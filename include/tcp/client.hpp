#ifndef CLIENT_HPP
#define CLIENT_HPP

/**
 * @file client.hpp
 * 
 * Client class that wraps asio networking code using TCP.
 */

#include <boost/asio.hpp>

#include <iostream>

#include "message.hpp"
#include "ts_deque.hpp"

#include "tcp/connection.hpp"

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
class client {
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
    bool Connect(const std::string& host, const uint16_t port) {
        try {
            // Resolve the host name and port number into a list of endpoints to try.
            boost::asio::ip::tcp::resolver resolver { m_asioContext };
            boost::asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));

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
    void Disconnect() {
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
    bool IsConnected() {
        return m_connection && m_connection->IsConnected();
    }

    /**
     * Sends a message to the server.
     * 
     * @param msg the message to send.
    */
    void Send(message<T>&& msg) {
        if (IsConnected()) {
            m_connection->Send(std::move(msg));
        }
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

    /// Thread for the context to execute its work commands independently.
    std::thread m_threadContext;

    /// The single instance of a connection object, which handles data transfer.
    std::unique_ptr<connection<T>> m_connection;

private:
    /// The thread-safe queue of messages from the server.
    ts_deque<tagged_message<T>> m_qMessagesIn;
};

} // namespace tcp

} // namespace flash

#endif