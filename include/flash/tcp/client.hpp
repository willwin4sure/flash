#ifndef FLASH_TCP_CLIENT_HPP
#define FLASH_TCP_CLIENT_HPP

/**
 * @file client.hpp
 * 
 * Client class that wraps asio networking code that uses the TCP protocol.
 */

#include <boost/asio.hpp>

#include <iostream>

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>
#include <flash/iclient.hpp>

#include <flash/tcp/connection.hpp>

namespace flash {

namespace tcp {

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

    /**
     * Default constructor for the client.
    */
    client() = default;

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
        try {
            // Resolve the host name and port number into a list of endpoints to try.
            boost::asio::ip::tcp::resolver resolver { m_asioContext };
            boost::asio::ip::tcp::resolver::results_type endpoints
                = resolver.resolve(host, std::to_string(port));

            // Create a client connection with a new socket.
            m_connection = std::make_unique<connection<T>>(
                connection<T>::owner::client,
                m_asioContext,                               // Provide the connection with the surrounding asio context.
                boost::asio::ip::tcp::socket(m_asioContext), // Create a new socket.
                m_qMessagesIn                                // Reference to the client's incoming message queue.
            );

            // Connect to the server.
            m_connection->ConnectToServer(endpoints);

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
            m_connection->Disconnect();
        }

        m_asioContext.stop();
        if (m_threadContext.joinable()) {
            m_threadContext.join();
        }

        m_connection.release();
    }

    /**
     * @returns Whether the client is connected to the server.
    */
    bool IsConnected() final {
        return m_connection && m_connection->IsConnected();
    }

    /**
     * Sends a message to the server.
     * 
     * @param msg the message to send.
    */
    void Send(message<T>&& msg) final {
        if (IsConnected()) {
            m_connection->Send(std::move(msg));
        }
    }

    /**
     * Returns a reference to the incoming message queue.
    */
    ts_deque<tagged_message<T>>& Incoming() final {
        return m_qMessagesIn;
    }

protected:
    boost::asio::io_context m_asioContext;        // The asio context for the client connection.
    std::thread m_threadContext;                  // Thread that runs the asio context.
    std::unique_ptr<connection<T>> m_connection;  // Handles data transfer.

private:
    ts_deque<tagged_message<T>> m_qMessagesIn;  // Thread-safe queue of incoming messages.
};

} // namespace tcp

} // namespace flash

#endif