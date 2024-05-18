#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <boost/asio.hpp>

#include <iostream>
#include <message.hpp>
#include <tsdeque.hpp>

namespace flash {

template <typename T>
class client {
public:
    client() : m_socket { m_context } {

    }

    virtual ~client() {
        Disconnect();
    }

    bool Connect(const std::string& host, const uint16_t port) {
        try {
            // Create a connection.
            m_connection = std::make_unique<connection<T>>();  // TODO

            // Resolve the host name and port number into an endpoint.
            boost::asio::ip::tcp::resolver resolver { m_context };
            auto m_endpoints = resolver.resolve(host, std::to_string(port));

            // Connect to the server.
            m_connection->ConnectToServer(m_endpoints);

            // Start running the context in its own thread.
            m_threadContext = std::thread([this]() { m_context.run(); });

        } catch (std::exception& e) {
            std::cerr << "Client Exception: " << e.what() << '\n';
            return false;
        }

        return true;
    }

    void Disconnect() {
        if (IsConnected()) {
            m_connection->Disconnect();
        }

        m_context.stop();
        if (m_threadContext.joinable()) {
            m_threadContext.join();
        }

        m_connection.release();
    }

    bool IsConnected() {
        return false;
    }

    /**
     * Returns a reference to the incoming message queue.
    */
    ts_deque<tagged_message<T>>& Incoming() {
        return m_qMessagesIn;
    }

protected:
    // The asio context for the client connection.
    boost::asio::io_context m_context;

    // Thread for the context to execute its work commands independently.
    std::thread m_threadContext;

    // The socket that is connected to the server.
    boost::asio::ip::tcp::socket m_socket;  // TODO: I'd prefer this not to be here.

    // The single instance of a connection object, which handles data transfer.
    std::unique_ptr<connection<T>> m_connection;

private:
    // The thread-safe queue of messages from the server.
    ts_deque<tagged_message<T>>& m_qMessagesIn;
};

} // namespace flash

#endif