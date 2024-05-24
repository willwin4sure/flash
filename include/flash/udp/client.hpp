#ifndef FLASH_UDP_CLIENT_HPP
#define FLASH_UDP_CLIENT_HPP

/**
 * @file client.hpp
 * 
 * Client class that wraps asio networking code using UDP.
 */

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

#include <iostream>

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>
#include <flash/scramble.hpp>
#include <flash/iclient.hpp>

#include <flash/udp/common.hpp>

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

    client()
        : m_socket(m_asioContext) {

    }

    /**
     * Virtual destructor for the client; disconnects the client from the server.
    */
    virtual ~client() {
        Disconnect();
    }

    /**
     * Connects the client to the server at the given host and port.
     * 
     * @param host the host name or IP address of the server.
     * @param port the port number of the server.
     * 
     * @returns Whether the connection was successful.
    */
    bool Connect(const std::string& host, const uint16_t port) final {
        std::cout << "Connecting to " << host << ':' << port << '\n';
        try {
            // Resolve the host name and port number into a list of endpoints to try.
            boost::asio::ip::udp::resolver resolver(m_asioContext);
            boost::asio::ip::udp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));

            m_socket = boost::asio::ip::udp::socket(m_asioContext);

            // Connect to the server.
            ConnectToServer(endpoints);

            // Start running the context in its own thread.
            m_threadContext = std::thread([this]() { m_asioContext.run(); });

        } catch (std::exception& e) {
            std::cout << "Client Exception: " << e.what() << '\n';
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
        if (IsConnected()) {
            boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
        }

        m_asioContext.stop();
        if (m_threadContext.joinable()) {
            m_threadContext.join();
        }
    }

    /**
     * Returns whether the client is connected to the server.
     * 
     * @returns Whether the client is connected to the server.
    */
    bool IsConnected() final {
        return m_socket.is_open();
    }

    /**
     * Sends a message to the server.
     * 
     * @param msg the message to send.
    */
    void Send(message<T>&& msg) final {
        // Add the message to the outgoing queue.
        // m_qMessagesOut.push_back({ std::move(msg), [this](const message<T>& msg) {
        //     // Send the message to the server.
        //     m_socket.async_send(boost::asio::buffer(msg.m_buffer.data(), msg.m_buffer.size()),
        //         [this](std::error_code ec, std::size_t length) {
        //         if (!ec) {
        //             // Message sent successfully.
        //         } else {
        //             std::cout << "Client Exception: " << ec.message() << '\n';
        //         }
        //     });
        // } });
    }

    /**
     * Returns a reference to the incoming message queue.
    */
    ts_deque<tagged_message<T>>& Incoming() {
        return m_qMessagesIn;
    }

protected:
    /// The asio context for the client connection.
    boost::asio::io_context m_asioContext;

    boost::asio::ip::udp::socket m_socket;

    /// Thread for the context to execute its work commands independently.
    std::thread m_threadContext;

    uint64_t m_handshakeIn;

private:
    /// The thread-safe queue of messages from the server.
    ts_deque<tagged_message<T>> m_qMessagesIn;

    ts_deque<message<T>> m_qMessagesOut;

    void ConnectToServer(const boost::asio::ip::udp::resolver::results_type& endpoints) {
        std::cout << "Connecting to server...\n";

        // Send a message to the server using UDP
        boost::asio::ip::udp::endpoint endpoint = *endpoints.begin();
        m_socket.open(endpoint.protocol());
        m_socket.connect(endpoint);

        std::cout << "Sending connection request...\n";

        // Send the magic number
        uint64_t magicNumber = boost::endian::native_to_big(CONNECTION_REQUEST_MAGIC_NUM);
        m_socket.async_send(boost::asio::buffer(&magicNumber, sizeof(uint64_t)),
            [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                std::cout << "Waiting for validation...\n";
                // Wait for the server to send the validation.
                HandleValidation();
            }
        });
    }

    void HandleValidation() {
        std::cout << "Handling validation...\n";
        // Wait for the handshake from the server.
        m_socket.async_receive(boost::asio::buffer(&m_handshakeIn, sizeof(uint64_t)),
            [this](std::error_code ec, std::size_t length) {
            std::cout << "Received handshake: " << m_handshakeIn << '\n';
            std::cout << "Received handshake: " << boost::endian::big_to_native(m_handshakeIn) << '\n';
            if (!ec) {
                uint64_t handshakeRes = boost::endian::native_to_big(Scramble(boost::endian::big_to_native(m_handshakeIn)));
                std::cout << "Sending handshake response: " << Scramble(boost::endian::big_to_native(m_handshakeIn)) << '\n';

                // Send the response back to the server.
                m_socket.async_send(boost::asio::buffer(&handshakeRes, sizeof(uint64_t)),
                    [this](std::error_code ec, std::size_t length) {
                    if (!ec) {
                        std::cout << "Connected to server.\n";
                        // Start receiving messages from the server.
                        // ReceiveMessages();
                    }
                });
            }
        });
    }

    // void ReceiveMessages() {
    //     m_socket.async_receive(boost::asio::buffer(m_qMessagesIn.emplace_back().m_msg.m_buffer.data(), message<T>::header_length),
    //         [this](std::error_code ec, std::size_t length) {
    //         if (!ec) {
    //             if (m_qMessagesIn.back().m_msg.decode_header()) {
    //                 // Now we need to read the body of the message.
    //                 ReadBody();
    //             }
    //         } else {
    //             std::cout << "Client Exception: " << ec.message() << '\n';
    //         }
    //     });
    // }

};

} // namespace udp

} // namespace flash

#endif