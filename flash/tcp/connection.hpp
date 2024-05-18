#ifndef CONNECTION_HPP
#define CONNECTION_HPP

/**
 * @file connection.hpp
 * 
 * Contains class that represents a TCP connection between a client and a server.
 * Abstracts away asio and asynchronous operations: the interface is just
 * a send operation for outgoing messages and a thread-safe queue of incoming messages.
*/

#include "flash/message.hpp"
#include "flash/ts_deque.hpp"

#include <boost/asio.hpp>

#include <memory>

namespace flash {

namespace tcp {

/**
 * Connection class that represents a connection between a client and a server,
 * owned by one of the sides.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
*/
template <typename T>
class connection {
public:

    /**
     * The type of the connection owner. Behavior is slightly different depending on the owner.
    */
    enum class owner : uint8_t {
        server,
        client
    };

    /**
     * Constructor for the connection. Sets up the connection with the given parameters.
     * 
     * @param ownerType the owner of the connection.
     * @param asioContext a reference to the asio context that the connection will run on.
     * @param socket the socket that the connection will use.
     * @param qMessagesIn a reference to the queue to deposit incoming messages into.
    */
    connection(owner ownerType, boost::asio::io_context& asioContext, boost::asio::ip::tcp::socket&& socket, ts_deque<tagged_message<T>>& qMessagesIn)
        : m_ownerType { ownerType }, m_asioContext { asioContext }, m_socket { std::move(socket) }, m_qMessagesIn { qMessagesIn } { }

    virtual ~connection() { }

    /**
     * @returns The ID of the other side of the connection.
     * This should be 0 if the owner is a client (since it is a connection to the server)
     * and the ID of the client is the owner is a server (since it is the connection to some client)
    */
    UserId GetId() const { return m_id; }

    /**
     * @returns The socket that the connection is using.
    */
    const boost::asio::ip::tcp::socket& GetSocket() { return m_socket; }

    /**
     * Turns the connection on by connecting it to a client,
     * and prompting it to continuously read header messages from the socket.
     * 
     * In order for this to do any work, the socket that we got
     * from the client connecting must be placed in this object and open.
     * 
     * @param uid the ID of the client to connect to.
    */
    void ConnectToClient(UserId uid = 0) {
        // Only the server should connect to clients.
        if (m_ownerType != owner::server) return;

        if (m_socket.is_open()) {
            // Set the ID of this connection to a client.
            m_id = uid;

            // Start waiting for message headers.
            ReadHeader();
        }
    }

    /**
     * Triggers this connection to start doing work by connecting it to the server,
     * and prompting it to continuously read header messages from the socket.
     * 
     * @param endpoints the results of a resolver operation that will be used to connect to the server.
    */
    void ConnectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
        // Only the client should connect to servers.
        if (m_ownerType != owner::client) return;

        boost::asio::async_connect(m_socket, endpoints,
            [this](std::error_code ec, boost::asio::ip::tcp::endpoint endpoint) {
                if (!ec) {
                    m_id = 0;
                    
                    // Start waiting for message headers.
                    ReadHeader();

                } else {
                    std::cerr << "Connect to server failed: " << ec.message() << '\n';
                }
            }
        );
    }

    /**
     * Disconnects the connection by closing the socket.
    */
    void Disconnect() {
        if (IsConnected()) {
            boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
        }
    }

    /**
     * @returns true if the connection is connected, false otherwise.
    */
    bool IsConnected() const {
        return m_socket.is_open();
    }

    /**
     * Sends a message to the remote side of the connection.
     * 
     * @param msg the message to send, moved in.
    */
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

    /// Identifier of the connection, initialized to max value and set when connected.
    UserId m_id { static_cast<UserId>(-1) };

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
        // Tell asio to wait for the header to fill the buffer and then run a callback.
        boost::asio::async_read(m_socket, boost::asio::buffer(&m_msgTemporaryIn.m_header, sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_msgTemporaryIn.m_header.m_size > 0) {
                        // If the message has a body, resize the body to fit the message and wait for it.
                        m_msgTemporaryIn.m_body.resize(m_msgTemporaryIn.m_header.m_size);
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
        // Tell asio to wait for the body to fill the buffer and then run a callback.
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
        // Tell asio to wait for the header to be written and then run a callback.
        boost::asio::async_write(m_socket, boost::asio::buffer(&m_qMessagesOut.front().m_header, sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_qMessagesOut.front().m_body.size() > 0) {
                        // If the message has a body, write it as well.
                        WriteBody();

                    } else {
                        // If the message has no body, remove it from the queue.
                        m_qMessagesOut.pop_front();

                        // Keep writing if there are more messages to send.
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
        // Tell asio to wait for the body to be written and then run a callback.
        boost::asio::async_write(m_socket, boost::asio::buffer(m_qMessagesOut.front().m_body.data(), m_qMessagesOut.front().m_body.size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // The header and body have both be written, so remove from the queue.
                    m_qMessagesOut.pop_front();

                    // Keep writing if there are more messages to send.
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

    /**
     * Asynchronous task for the asio context to add a message to the incoming message queue.
    */
    void AddToIncomingMessageQueue() {
        m_qMessagesIn.push_back(tagged_message<T>{ GetId(), m_msgTemporaryIn });

        // Need to keep the asio context busy.
        ReadHeader();
    }
};

} // namespace tcp

} // namespace flash

#endif