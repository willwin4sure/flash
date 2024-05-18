#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "message.hpp"
#include "tsdeque.hpp"

#include <boost/asio.hpp>

#include <memory>

namespace flash {

/**
 * Connection class that represents a connection between a client and a server.
 * 
 * @tparam T The type of the message to be sent.
 *
 * @note that we subclass on std::enable_shared_from_this to allow any shared_ptr
 * to this connection to generate another shared_ptr for the connection, which is
 * necessary for the server to distribute multiple shared_ptrs to the same connection.
*/
template <typename T>
class connection : public std::enable_shared_from_this<connection<T>> {
public:

    /**
     * The type of the connection owner. Behavior is slightly different depending on the owner.
    */
    enum class owner {
        server,
        client
    };

    connection(
        owner parent,
        boost::asio::io_context& asioContext,
        boost::asio::ip::tcp::socket socket,
        ts_deque<tagged_message<T>>& qMessagesIn
    ) : m_ownerType { parent }, m_asioContext { asioContext }, m_socket { std::move(socket) }, m_qMessagesIn { qMessagesIn } { }

    virtual ~connection() {}

    uint32_t GetId() const { return m_id; }

    void ConnectToClient(uint32_t uid = 0) {
        // Only the server should connect to clients.
        if (m_ownerType == owner::server) {
            if (m_socket.is_open()) {
                m_id = uid;

                // Start waiting for message headers.
                ReadHeader();
            }            
        }
    }

    void ConnectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
        // Only the client should connect to servers.
        if (m_ownerType == owner::client) {
            boost::asio::async_connect(m_socket, endpoints,
                [this](std::error_code ec, boost::asio::ip::tcp::endpoint endpoint) {
                    if (!ec) {
                        // Start waiting for message headers.
                        ReadHeader();
                    } else {
                        std::cerr << "Connect to server failed: " << ec.message() << '\n';
                    }
                }
            );
        }
    }

    void Disconnect() {
        if (IsConnected()) {
            boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
        }
    }

    bool IsConnected() const {
        return m_socket.is_open();
    }

    void Send(message<T>&& msg) {
        boost::asio::post(m_asioContext,
            // Black magic generalized lambda capture from
            // https://stackoverflow.com/questions/8640393/move-capture-in-lambda
            [this, msg = std::move(msg)] () mutable {
                bool areWritingMessage = !m_qMessagesOut.empty();
                m_qMessagesOut.push_back(std::move(msg));

                // If writing is already occurring, no need to start the loop again.
                if (!areWritingMessage) {
                    WriteHeader();
                }
            }
        );
    }

protected:
    /// Type of the connection owner.
    owner m_ownerType { owner::server };

    /// Identifier of the client connection.
    uint32_t m_id { 0 };

    /// Each connection has a unique socket that is connected to a remote; we own it.
    boost::asio::ip::tcp::socket m_socket;

    /// The asio context is shared with all other connections.
    boost::asio::io_context& m_asioContext;

    /// Queue holding messages to be sent to the remote; we own it.
    ts_deque<message<T>> m_qMessagesOut;

    /// Queue holding messages received from the remote side, owned by the client or server.
    /// This design choice is so that all incoming messages are serialized; this is also
    /// why we have to tag the messages with the connection they came from.
    ts_deque<tagged_message<T>>& m_qMessagesIn;

    /// Temporary message to hold incoming message data, initialized with garbage.
    message<T> m_msgTemporaryIn { static_cast<T>(0) };

private:
    /**
     * Asynchronous task for the asio context, waiting to read a message header of appropriate size.
    */
    void ReadHeader() {
        boost::asio::async_read(m_socket, boost::asio::buffer(&m_msgTemporaryIn.m_header, sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_msgTemporaryIn.m_header.size > 0) {
                        // If the message has a body, resize the body to fit the message and wait for it.
                        m_msgTemporaryIn.m_body.resize(m_msgTemporaryIn.m_header.size - sizeof(header<T>));
                        ReadBody();

                    } else {
                        // If the message has no body, add it to the incoming message queue.
                        AddToIncomingMessageQueue();
                    }

                } else {
                    std::cerr << "[" << m_id << "] Read Header Fail: " << ec.message() << '\n';
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context, waiting to read a message body.
    */
    void ReadBody() {
        boost::asio::async_read(m_socket, boost::asio::buffer(m_msgTemporaryIn.m_body.data(), m_msgTemporaryIn.m_body.size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // Header and body have both been read, so add the message to incoming queue.
                    AddToIncomingMessageQueue();

                } else {
                    std::cerr << "[" << m_id << "] Read Body Fail: " << ec.message() << '\n';
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context to write a message header.
    */
    void WriteHeader() {
        boost::asio::async_write(m_socket, boost::asio::buffer(&m_qMessagesOut.front().m_header, sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_qMessagesOut.front().m_body.size() > 0) {
                        WriteBody();

                    } else {
                        m_qMessagesOut.pop_front();

                        if (!m_qMessagesOut.empty()) {
                            WriteHeader();
                        }
                    }

                } else {
                    std::cerr << "[" << m_id << "] Write Header Fail: " << ec.message() << '\n';
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context to write a message body.
    */
    void WriteBody() {
        boost::asio::async_write(m_socket, boost::asio::buffer(m_qMessagesOut.front().m_body.data(), m_qMessagesOut.front().m_body.size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    m_qMessagesOut.pop_front();

                    if (!m_qMessagesOut.empty()) {
                        WriteHeader();
                    }

                } else {
                    std::cerr << "[" << m_id << "] Write Body Fail: " << ec.message() << '\n';
                    m_socket.close();
                }
            }
        );
    }

    void AddToIncomingMessageQueue() {
        if (m_ownerType == owner::server) {
            // Server needs to tag the message with the client that sent it.
            m_qMessagesIn.push_back(tagged_message<T>{ this->shared_from_this(), m_msgTemporaryIn });
        } else {
            // Clients can only talk to server so no need to tag.
            m_qMessagesIn.push_back(tagged_message<T>{ nullptr, m_msgTemporaryIn });
        }

        // Need to keep the asio context busy.
        ReadHeader();
    }
};

} // namespace flash

#endif