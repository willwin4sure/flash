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
        if (m_ownerType == owner::server) {
            // Only the server should connect to clients.
            if (m_socket.is_open()) {
                m_id = uid;

                // Start waiting for message headers.
                ReadHeader();
            }            
        }
    }

    bool ConnectToServer();

    bool Disconnect() {
        if (IsConnected()) {
            boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
            return true;
        }

        return false;
    }

    bool IsConnected() const {
        return m_socket.is_open();
    }

    bool Send(message<T>&& msg) {
        boost::asio::post(m_asioContext,
            [this, msg]() {
                bool areWritingMessage = !m_qMessagesOut.empty();
                m_qMessagesOut.push_back(std::move(msg));

                if (!areWritingMessage) {
                    // If writing is already occurring, no need to start it again.
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

    /// Temporary message to hold incoming message data.
    message<T> m_msgTemporaryIn;

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
                        m_msgTemporaryIn.m_body.resize(m_msgTemporaryIn.m_header.size);
                        ReadBody();

                    } else {
                        // If the message has no body, add it to the incoming message queue.
                        AddToIncomingMessageQueue();
                    }

                } else {
                    std::cout << "[" << m_id << "] Read Header Fail.\n";
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
                    std::cout << "[" << m_id << "] Read Body Fail.\n";
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context to write a message header.
    */
    void WriteHeader() {
        boost::asio::async_write(m_socket, asio::buffer(&m_qMessagesOut.front().m_header, sizeof(header<T>)),
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
                    std::cout << "[" << m_id << "] Write Header Fail.\n";
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context to write a message body.
    */
    void WriteBody() {
        boost::asio::async_write(m_socket, asio::buffer(m_qMessagesOut.front().m_body.data(), m_qMessagesOut.front().m_body.size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    m_qMessagesOut.pop_front();

                    if (!m_qMessagesOut.empty()) {
                        WriteHeader();
                    }

                } else {
                    std::cout << "[" << m_id << "] Write Body Fail.\n";
                    m_socket.close();
                }
            }
        );
    }

    void AddToIncomingMessageQueue() {
        if (m_ownerType == owner::server) {
            // Server needs to tag the message with the client that sent it.
            m_qMessagesIn.push_back({ this->shared_from_this(), m_msgTemporaryIn });
        } else {
            // Clients can only talk to server so no need to tag.
            m_qMessagesIn.push_back({ nullptr, m_msgTemporaryIn });
        }

        // Need to keep the asio context busy.
        ReadHeader();
    }
};

} // namespace flash

#endif