#ifndef FLASH_TCP_CONNECTION_HPP
#define FLASH_TCP_CONNECTION_HPP

/**
 * @file connection.hpp
 * 
 * Contains class that represents a TCP connection between a client and a server.
 * Abstracts away asio and asynchronous operations: the interface is just
 * a send operation for outgoing messages and providing a thread-safe queue
 * to place the incoming messages.
*/

#include <flash/message.hpp>
#include <flash/scramble.hpp>
#include <flash/ts_deque.hpp>

#include <flash/iserverext.hpp>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

#include <chrono>
#include <memory>
#include <sstream>

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
     * The type of the connection owner. Behavior is different depending on the owner.
    */
    enum class owner : uint8_t {
        server,
        client
    };

    /**
     * Constructor for the connection. Sets up the connection with the given parameters.
     * 
     * @param ownerType   the owner of the connection.
     * @param asioContext a reference to the asio context that the connection will run on.
     * @param socket      the socket that the connection will use.
     * @param qMessagesIn a reference to the queue to deposit incoming messages into.
    */
    connection(owner ownerType,
               boost::asio::io_context& asioContext,
               boost::asio::ip::tcp::socket&& socket,
               ts_deque<tagged_message<T>>& qMessagesIn)
               
        : m_ownerType { ownerType }, m_asioContext { asioContext },
          m_socket { std::move(socket) }, m_qMessagesIn { qMessagesIn } {

        if (m_ownerType == owner::server) {
            // Server needs to generate random data for client to validate on.
            m_handshakeOut = Scramble(
                uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));

            // What the client should return to us during the handshake.
            m_handshakeCheck = Scramble(m_handshakeOut);
        }
    }

    /**
     * Virtual destructor for the connection.
    */
    virtual ~connection() { }

    /**
     * @returns The ID of the remote side of the connection.
     * 
     * This should be 0 if the owner is a client (remote is the server),
     * or the ID of the client is the owner is a server (remote is some client).
    */
    UserId GetId() const { return m_id; }

    /**
     * @returns The socket that the connection is using.
    */
    const boost::asio::ip::tcp::socket& GetSocket() { return m_socket; }

    /**
     * Turns the connection on by connecting it to a client, prompting it
     * to complete validation checks, and then continuously read header
     * messages from the socket.
     * 
     * In order for this to do any work, the socket that we got
     * from the connecting client must be placed in this object and open.
     * This is so that we can talk back at the client.
     * 
     * @param uid the ID of the client to connect to.
    */
    void ConnectToClient(UserId uid, iserverext<T>* server = nullptr) {
        // Only the server should connect to clients.
        if (m_ownerType != owner::server) return;

        if (m_socket.is_open()) {
            m_id = uid;

            // Send the validation challenge to the client.
            WriteValidation();

            // Wait asynchronously for the client to respond.
            ReadValidation(server);
        }
    }

    /**
     * Triggers this connection to start doing work by connecting it to the server,
     * and prompting it to continuously read header messages from the socket.
     * 
     * @param endpoints the results of a resolver operation used to connect to the server.
    */
    void ConnectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
        // Only the client should connect to servers.
        if (m_ownerType != owner::client) return;

        boost::asio::async_connect(
            m_socket, endpoints,
            [this](std::error_code ec, boost::asio::ip::tcp::endpoint endpoint) {
                if (!ec) {
                    m_id = SERVER_USER_ID;
                    
                    // Wait for the validation challenge from the server.
                    ReadValidation();

                } else {
                    std::stringstream ss;
                    ss << "Connect to server failed: " << ec.message() << "\n";
                    std::cout << ss.str();
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
        boost::asio::post(
            m_asioContext,
            
            // Black magic generalized lambda capture from
            // https://stackoverflow.com/questions/8640393/move-capture-in-lambda

            [this, msg = std::move(msg)] () mutable {
                bool writing = !m_qMessagesOut.empty();
                msg.get_header().m_size = boost::endian::native_to_big(msg.get_header().m_size);
                m_qMessagesOut.push_back(std::move(msg));

                // If writing is already occurring, no need to start the loop again.
                if (!writing) {
                    WriteHeader();
                }
            }
        );
    }

protected:
    owner m_ownerType { owner::server };     // Identifies the owner of the connection.
    UserId m_id { INVALID_USER_ID };         // Identifies the remote side of connection.

    boost::asio::io_context& m_asioContext;  // Shared asio context among connections.
    boost::asio::ip::tcp::socket m_socket;   // Unique socket connected to remote, owned.

    ts_deque<message<T>> m_qMessagesOut;     // Queue of messages to send, owned.

    message<T> m_msgTemporaryIn { static_cast<T>(0) };  // Holds incoming message data.

    /// Queue holding messages received from the remote side, owned by the caller.
    /// This design choice is so that all incoming messages are serialized; this is also
    /// why we have to tag the messages with the connection they came from.
    ts_deque<tagged_message<T>>& m_qMessagesIn;  

    uint64_t m_handshakeOut { 0 };    // Outgoing handshake data (challenge or response)
    uint64_t m_handshakeIn { 0 };     // Incoming handshake data (challenge or response)
    uint64_t m_handshakeCheck { 0 };  // Correct handshake response

private:
    /**
     * Asynchronous task for the asio context.
     * 
     * Reads a validation challenge or response.
    */
    void ReadValidation(iserverext<T>* server = nullptr) {
        boost::asio::async_read(
            m_socket, boost::asio::buffer(&m_handshakeIn, sizeof(uint64_t)),
            [this, server](std::error_code ec, std::size_t length) {
                if (!ec) {
                    m_handshakeIn = boost::endian::big_to_native(m_handshakeIn);

                    if (m_ownerType == owner::server) {
                        // Server has received the validation data.
                        // Check it against the expected value.
                        if (m_handshakeIn == m_handshakeCheck) {
                            std::stringstream ss;
                            ss << "[" << m_id << "] Client Validated.\n";
                            std::cout << ss.str();

                            if (server) server->OnClientValidate(m_id);

                            ReadHeader();
                            
                        } else {
                            std::stringstream ss;
                            ss << "[" << m_id << "] Client Failed Validation.\n";
                            std::cout << ss.str();

                            m_socket.close();
                        }

                    } else {
                        // Client has received the validation data, send response.
                        m_handshakeOut = Scramble(m_handshakeIn);
                        WriteValidation();
                    }

                } else {
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context.
     * 
     * Writes a validation challenge or response.
    */
    void WriteValidation() {
        uint64_t handshakeOut = boost::endian::native_to_big(m_handshakeOut);

        boost::asio::async_write(
            m_socket, boost::asio::buffer(&handshakeOut, sizeof(uint64_t)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_ownerType == owner::client) {
                        // Sent the validation data, just wait for messages (or closure)
                        ReadHeader();
                    }

                } else {
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context.
     * 
     * Reads a message header of appropriate size.
    */
    void ReadHeader() {
        // Tell asio to wait for the header to fill the buffer and then run a callback.
        boost::asio::async_read(
            m_socket, boost::asio::buffer(&m_msgTemporaryIn.get_header(), sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    m_msgTemporaryIn.get_header().m_size = boost::endian::big_to_native(m_msgTemporaryIn.get_header().m_size);

                    // Resize the temporary body to the right size. Crucial if the
                    // body is empty now and you need to clear the old temporary!
                    m_msgTemporaryIn.get_body().resize(m_msgTemporaryIn.get_header().m_size);

                    if (m_msgTemporaryIn.get_header().m_size > 0) {
                        // If the message has a body, wait for it.
                        ReadBody();

                    } else {
                        // If the message has no body, add it to the incoming message queue.
                        AddToIncomingMessageQueue();
                    }

                } else {
                    std::stringstream ss;
                    ss << "[" << m_id << "] Read Header Fail: " << ec.message() << '\n';
                    std::cout << ss.str();
                    
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context.
     * 
     * Reads a message body.
    */
    void ReadBody() {
        // Tell asio to wait for the body to fill the buffer and then run a callback.
        boost::asio::async_read(
            m_socket, boost::asio::buffer(m_msgTemporaryIn.get_body().data(), m_msgTemporaryIn.get_body().size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // Header and body have both been read, so add the message to incoming queue.
                    AddToIncomingMessageQueue();

                } else {
                    std::stringstream ss;
                    ss << "[" << m_id << "] Read Body Fail: " << ec.message() << '\n';
                    std::cout << ss.str();
                    
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context.
     * 
     * Writes a message header.
    */
    void WriteHeader() {
        // Tell asio to wait for the header to be written and then run a callback.
        boost::asio::async_write(
            m_socket, boost::asio::buffer(&m_qMessagesOut.front().get_header(), sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_qMessagesOut.front().get_body().size() > 0) {
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
                    std::stringstream ss;
                    ss << "[" << m_id << "] Write Header Fail: " << ec.message() << '\n';
                    std::cout << ss.str();

                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context.
     * 
     * Writes a message body.
    */
    void WriteBody() {
        // Tell asio to wait for the body to be written and then run a callback.
        boost::asio::async_write(
            m_socket, boost::asio::buffer(m_qMessagesOut.front().get_body().data(),
                                          m_qMessagesOut.front().get_body().size()),

            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // The header and body have both be written, so remove from the queue.
                    m_qMessagesOut.pop_front();

                    // Keep writing if there are more messages to send.
                    if (!m_qMessagesOut.empty()) {
                        WriteHeader();
                    }

                } else {
                    std::stringstream ss;
                    ss << "[" << m_id << "] Write Body Fail: " << ec.message() << '\n';
                    std::cout << ss.str();
                    
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context.
     * 
     * Adds a message to the incoming message queue.
    */
    void AddToIncomingMessageQueue() {
        m_qMessagesIn.push_back(tagged_message<T>{ GetId(), std::move(m_msgTemporaryIn) });

        // Need to keep the asio context busy.
        ReadHeader();
    }
};

} // namespace tcp

} // namespace flash

#endif