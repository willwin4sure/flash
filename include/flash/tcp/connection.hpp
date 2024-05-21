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
#include <flash/ts_deque.hpp>

#include <flash/iserverext.hpp>

#include <boost/asio.hpp>

#include <chrono>
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
     * @param ownerType   the owner of the connection.
     * @param asioContext a reference to the asio context that the connection will run on.
     * @param socket      the socket that the connection will use.
     * @param qMessagesIn a reference to the queue to deposit incoming messages into.
    */
    connection(owner ownerType, boost::asio::io_context& asioContext, boost::asio::ip::tcp::socket&& socket, ts_deque<tagged_message<T>>& qMessagesIn)
        : m_ownerType { ownerType }, m_asioContext { asioContext }, m_socket { std::move(socket) }, m_qMessagesIn { qMessagesIn } {

        if (m_ownerType == owner::server) {
            // Server needs to generate random data for client to validate on.
            m_handshakeOut = Scramble(uint64_t(std::chrono::system_clock::now().time_since_epoch().count()));

            // What the client should return to us during the handshake.
            m_handshakeCheck = Scramble(m_handshakeOut);
        }
    }

    virtual ~connection() { }

    /**
     * @returns The ID of the other side of the connection.
     * 
     * This should be 0 if the owner is a client (since it is a connection to the server),
     * or the ID of the client is the owner is a server (since it is the connection to some client).
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
     * @param endpoints the results of a resolver operation that will be used to connect to the server.
    */
    void ConnectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
        // Only the client should connect to servers.
        if (m_ownerType != owner::client) return;

        boost::asio::async_connect(m_socket, endpoints,
            [this](std::error_code ec, boost::asio::ip::tcp::endpoint endpoint) {
                if (!ec) {
                    m_id = SERVER_USER_ID;
                    
                    // Wait for the validation challenge from the server.
                    ReadValidation();

                } else {
                    std::cout << "Connect to server failed: " << ec.message() << '\n';
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

    /// Identifier of the remote side of the connection.
    UserId m_id { INVALID_USER_ID };

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

    /// Temporary message to hold incoming message data, initialized with empty body and header type 0.
    message<T> m_msgTemporaryIn { static_cast<T>(0) };

    uint64_t m_handshakeOut { 0 };
    uint64_t m_handshakeIn { 0 };
    uint64_t m_handshakeCheck { 0 };

private:
    /**
     * Asynchronous task for the asio context, reading a validation challenge or response.
    */
    void ReadValidation(iserverext<T>* server = nullptr) {
        boost::asio::async_read(m_socket, boost::asio::buffer(&m_handshakeIn, sizeof(uint64_t)),
            [this, server](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_ownerType == owner::server) {
                        // Server has received the validation data, should check it and then wait for messages.
                        if (m_handshakeIn == m_handshakeCheck) {
                            std::cout << "[" << m_id << "] Client Validated.\n";

                            if (server) server->OnClientValidate(m_id);

                            ReadHeader();
                            
                        } else {
                            std::cout << "Client Disconnected (Failed Validation).\n";
                            m_socket.close();
                        }

                    } else {
                        // Client has received the validation data, should send the response.
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
     * Asynchronous task for the asio context, writing a validation challenge or response.
    */
    void WriteValidation() {
        boost::asio::async_write(m_socket, boost::asio::buffer(&m_handshakeOut, sizeof(uint64_t)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    if (m_ownerType == owner::client) {
                        // Client has sent the validation data, should just wait for messages (or closure)
                        ReadHeader();
                    }

                } else {
                    m_socket.close();
                }
            }
        );
    }

    /**
     * Asynchronous task for the asio context, waiting to read a message header of appropriate size.
    */
    void ReadHeader() {
        // Tell asio to wait for the header to fill the buffer and then run a callback.
        boost::asio::async_read(m_socket, boost::asio::buffer(&m_msgTemporaryIn.get_header(), sizeof(header<T>)),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // Resize the temporary body to the right size. Crucial if the body is empty now
                    // and you need to clear the old temporary!
                    m_msgTemporaryIn.get_body().resize(m_msgTemporaryIn.get_header().m_size);

                    if (m_msgTemporaryIn.get_header().m_size > 0) {
                        // If the message has a body, wait for it.
                        ReadBody();

                    } else {
                        // If the message has no body, add it to the incoming message queue.
                        AddToIncomingMessageQueue();
                    }

                } else {
                    std::cout << "[" << m_id << "] Read Header Fail: " << ec.message() << '\n';
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
        boost::asio::async_read(m_socket, boost::asio::buffer(m_msgTemporaryIn.get_body().data(), m_msgTemporaryIn.get_body().size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // Header and body have both been read, so add the message to incoming queue.
                    AddToIncomingMessageQueue();

                } else {
                    std::cout << "[" << m_id << "] Read Body Fail: " << ec.message() << '\n';
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
        boost::asio::async_write(m_socket, boost::asio::buffer(&m_qMessagesOut.front().get_header(), sizeof(header<T>)),
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
                    std::cout << "[" << m_id << "] Write Header Fail: " << ec.message() << '\n';
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
        boost::asio::async_write(m_socket, boost::asio::buffer(m_qMessagesOut.front().get_body().data(), m_qMessagesOut.front().get_body().size()),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // The header and body have both be written, so remove from the queue.
                    m_qMessagesOut.pop_front();

                    // Keep writing if there are more messages to send.
                    if (!m_qMessagesOut.empty()) {
                        WriteHeader();
                    }

                } else {
                    std::cout << "[" << m_id << "] Write Body Fail: " << ec.message() << '\n';
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

    /**
     * Mixes 64 bits into 32 bits with improved entropy.
    */
    static uint32_t MixBits(uint64_t x) {
        x = x ^ 0xa0b1c2d3;
        uint32_t xor_shifted = ((x >> 18u) ^ x) >> 27u;
        uint32_t rot = x >> 59u;
        uint32_t res = (xor_shifted >> rot) | (xor_shifted << ((-rot) & 31));
        return res ^ 0x12345678;
    }

    /**
     * Scrambles the input using a rather random function.
    */
    static uint64_t Scramble(uint64_t input) {
        static constexpr uint64_t LARGE_PRIME = 6364136223846793005ULL;
        static constexpr uint64_t OFFSET      = 000'001'000;  // Encodes the version of the protocol.

        return static_cast<uint64_t>(MixBits(static_cast<uint64_t>(MixBits(input)) * LARGE_PRIME + OFFSET)) * LARGE_PRIME + OFFSET;
    }
};

} // namespace tcp

} // namespace flash

#endif