#ifndef FLASH_UDP_CLIENT_HPP
#define FLASH_UDP_CLIENT_HPP

/**
 * @file client.hpp
 * 
 * Client class that wraps asio networking code using UDP.
 */


#include <flash/message.hpp>
#include <flash/ts_deque.hpp>
#include <flash/scramble.hpp>
#include <flash/iclient.hpp>

#include <flash/udp/common.hpp>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

#include <iostream>
#include <sstream>

namespace flash {

namespace udp {

/**
 * Client class that handles connection to the server.
 * 
 * Provides an interface to connect to a server, send messages,
 * and receive messages through a thread-safe queue.
 * 
 * Should be inherited for custom functionality.
 * 
 * @tparam T the message type to send and receive.
*/
template <typename T>
class client : public iclient<T> {
public:
    client(uint32_t clientTimeout = 5000)
        : m_socket(m_asioContext), m_clientTimeout { clientTimeout } {

        m_tempBufferIn.resize(MAX_MESSAGE_SIZE_IN_BYTES);

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        m_lastMessageTime = now;
    }

    virtual ~client() { }

    /**
     * Connects the client to the server at the given host and port.
     * 
     * @param host the host name or IP address of the server.
     * @param port the port number of the server.
     * 
     * @returns Whether the connection was successful.
    */
    bool Connect(const std::string& host, const uint16_t port) final {
        std::stringstream ss;
        ss << "Connecting to " << host << ':' << port << '\n';
        std::cout << ss.str();

        try {
            // Resolve the host name and port number into a list of endpoints to try.
            boost::asio::ip::udp::resolver resolver(m_asioContext);
            boost::asio::ip::udp::resolver::results_type endpoints
                = resolver.resolve(host, std::to_string(port));

            m_socket = boost::asio::ip::udp::socket(m_asioContext);

            // Connect to the server.
            ConnectToServer(endpoints);

            // Start running the context in its own thread.
            m_threadContext = std::thread([this]() { m_asioContext.run(); });

        } catch (std::exception& e) {
            std::stringstream ss;
            ss << "Client Exception: " << e.what() << '\n';
            std::cout << ss.str();

            return false;
        }

        return true;
    }

    /**
     * Disconnects the client from the server.
     * 
     * Stops the asio context and joins the context thread,
     * also releases the unique pointer to the connection.
    */
    void Disconnect() final {
        boost::asio::post(m_asioContext, [this]() { m_asioContext.stop(); });
        
        if (m_socket.is_open()) {
            boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
        }

        if (m_threadContext.joinable()) {
            m_threadContext.join();
        }

        std::cout << "Client Disconnected.\n";
    }

    /**
     * Returns whether the client is connected to the server.
     * 
     * @returns Whether the client is connected to the server.
    */
    bool IsConnected() final {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastMessageTime).count() > m_clientTimeout) {
            return false;
        }

        return true;
    }

    /**
     * Sends a message to the server.
     * 
     * @param msg the message to send.
    */
    void Send(message<T>&& msg) final {
        // Message is too long, reject.
        if (msg.size() > MAX_MESSAGE_SIZE_IN_BYTES) {
            assert(false);
            return;
        }

        boost::asio::post(
            m_asioContext,

            // Black magic generalized lambda capture from
            // https://stackoverflow.com/questions/8640393/move-capture-in-lambda

            [this, msg = std::move(msg)]() mutable {
                bool writing = !m_qMessagesOut.empty();
                msg.get_header().m_size = boost::endian::native_to_big(msg.get_header().m_size);
                m_qMessagesOut.push_back(std::move(msg));

                if (!writing) {
                    SendMessages();
                }
            }
        );
    }

    /**
     * Returns a reference to the incoming message queue.
    */
    ts_deque<tagged_message<T>>& Incoming() {
        return m_qMessagesIn;
    }

protected:
    boost::asio::io_context m_asioContext;  // The asio context for the client connection.
    std::thread m_threadContext;            // Thread that runs the asio context.
    boost::asio::ip::udp::socket m_socket;  // Socket for the client connection.

    std::vector<uint8_t> m_tempBufferIn;    // Buffer to store incoming messages.
    std::vector<uint8_t> m_tempBufferOut;   // Buffer to store outgoing messages.

    uint64_t m_magicNumOut;       // Magic number for connection.
    uint64_t m_tempHandshakeIn;   // Temporary handshake value for receiving.
    uint64_t m_tempHandshakeOut;  // Temporary handshake value for sending.

    uint32_t m_clientTimeout;  // Timeout for client connection.

    std::chrono::steady_clock::time_point m_lastMessageTime;  // Time of last message received.

private:
    ts_deque<tagged_message<T>> m_qMessagesIn;  // Queue of incoming messages.
    ts_deque<message<T>> m_qMessagesOut;        // Queue of outgoing messages.

    void ConnectToServer(const boost::asio::ip::udp::resolver::results_type& endpoints) {
        // Try connecting via the first endpoint.
        boost::asio::ip::udp::endpoint endpoint = *endpoints.begin();
        m_socket.open(endpoint.protocol());
        m_socket.connect(endpoint);

        // Send the magic number
        m_magicNumOut = boost::endian::native_to_big(CONNECTION_REQUEST_MAGIC_NUMBER);
        m_socket.async_send(
            boost::asio::buffer(&m_magicNumOut, sizeof(uint64_t)),
            [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                // Wait for the server to send the validation.
                HandleValidation();
            }
        });
    }

    void HandleValidation() {
        // Wait for the handshake from the server.
        m_socket.async_receive(
            boost::asio::buffer(&m_tempHandshakeIn, sizeof(uint64_t)),
            [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                m_tempHandshakeIn = boost::endian::big_to_native(m_tempHandshakeIn);
                m_tempHandshakeOut = Scramble(m_tempHandshakeIn);
                m_tempHandshakeOut = boost::endian::native_to_big(m_tempHandshakeOut);

                // Send the response back to the server.
                m_socket.async_send(
                    boost::asio::buffer(&m_tempHandshakeOut, sizeof(uint64_t)),
                    [this](std::error_code ec, std::size_t length) {
                    if (!ec) {
                        std::cout << "Connected to server.\n";

                        // Start receiving messages from the server.
                        ReceiveMessages();
                    }
                });
            }
        });
    }

    void ProcessMessage(std::size_t length) {
        // Message is too short to be in the canonical format, ignore
        if (length < sizeof(header<T>)) return;

        message<T> msg { static_cast<T>(0) };
        std::memcpy(&msg.get_header(), m_tempBufferIn.data(), sizeof(header<T>));
        msg.get_header().m_size = boost::endian::big_to_native(msg.get_header().m_size);

        // Size of message body does not match the size in the header, ignore
        if (length - sizeof(header<T>) != msg.get_header().m_size) return;

        msg.get_body().resize(msg.get_header().m_size);
        std::memcpy(msg.get_body().data(), m_tempBufferIn.data() + sizeof(header<T>), msg.get_header().m_size);

        m_lastMessageTime = std::chrono::steady_clock::now();
        m_qMessagesIn.push_back(tagged_message<T> { SERVER_USER_ID, std::move(msg) });
    }

    void ReceiveMessages() {
        m_socket.async_receive(
            boost::asio::buffer(m_tempBufferIn.data(), m_tempBufferIn.size()),
            [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                ProcessMessage(length);
                ReceiveMessages();

            } else {
                std::stringstream ss;
                ss << "Client Exception: " << ec.message() << '\n';
                std::cout << ss.str();
            }
        });
    }

    void SendMessages() {
        if (m_qMessagesOut.empty()) return;

        message<T> msg = m_qMessagesOut.front();

        m_tempBufferOut.resize(msg.size());

        std::memcpy(m_tempBufferOut.data(), &msg.get_header(), sizeof(header<T>));
        std::memcpy(m_tempBufferOut.data() + sizeof(header<T>), msg.get_body().data(), msg.get_body().size());

        m_socket.async_send(
            boost::asio::buffer(m_tempBufferOut.data(), m_tempBufferOut.size()),
            [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                m_qMessagesOut.pop_front();

                if (!m_qMessagesOut.empty()) {
                    SendMessages();
                }
            } else {
                std::stringstream ss;
                ss << "Client Exception: " << ec.message() << '\n';
                std::cout << ss.str();
            }
        });
    }

};

} // namespace udp

} // namespace flash

#endif