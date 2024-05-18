#ifndef SERVER_HPP
#define SERVER_HPP

#include "connection.hpp"
#include "message.hpp"
#include "tsdeque.hpp"

#include <iostream>

namespace flash {

template <typename T>
class server {
public:
    /**
     * Constructor for the server. Sets up the acceptor to listen for incoming connections.
    */
    server(uint16_t port) 
        : m_asioAcceptor(m_asioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    }

    /**
     * Virtual destructor for the server. Stops the server if it is still running.
    */
    virtual ~server() {
        Stop();
    }

    /**
     * Start the server. It will begin listening for clients on the asio context thread.
    */
    bool Start() {
        try {
            // Issue a task to the asio context to listen for clients.
            WaitForClientConnection();

            // Start the asio context in its own thread. We issue the work before
            // to guarantee that the thread doesn't immediately stop.
            m_threadContext = std::thread([this]() { m_asioContext.run(); });

        } catch (std::exception& e) {
            // Something prohibited the server from starting, print the error.
            std::cerr << "[SERVER] Exception: " << e.what() << '\n';
            return false;
        }

        std::cout << "[SERVER] Started!\n";
        return true;
    }

    /**
     * Stop the server. Request the context to close, and then wait on the thread to join it.
    */
    void Stop() {
        // Request the context to close.
        m_asioContext.stop();

        // Wait for the context thread to finish.
        if (m_threadContext.joinable()) m_threadContext.join();

        std::cout << "[SERVER] Stopped!\n";
    }

    /**
     * Asynchronous task for the asio context thread, waiting for a client to connect.
     * 
     * This task should be constantly running on the server, even if no clients are around.
    */
    void WaitForClientConnection() {
        m_asioAcceptor.async_accept(
            [this](std::error_code ec, boost::asio::ip::tcp::socket socket) {
                if (!ec) {
                    std::cout << "[SERVER] New Connection from IP: " << socket.remote_endpoint() << '\n';

                    // Make a new connection.
                    std::shared_ptr<connection<T>> new_connection = std::make_shared<connection<T>>(
                        connection<T>::owner::server,
                        m_asioContext,     // Provide the connection with the surrounding asio context.
                        std::move(socket), // Move the new socket into the connection.
                        m_qMessagesin      // Reference to the server's incoming message queue.
                    );

                    // Give the user a chance to deny connection by overriding OnClientConnect.
                    if (OnClientConnect(new_connection)) {
                        // Transfer ownership of the new connection to the server.
                        m_activeConnections.push_back(std::move(new_connection));

                        // Tell the connection to connect to the client and assign them a unique ID.
                        m_activeConnections.back()->ConnectToClient(m_uidCounter++);

                        std::cout << "[" << m_activeConnections.back()->GetId() << "] Connection Approved\n";

                    } else {
                        std::cout << "[------] Connection Denied\n";
                    }

                } else {
                    std::cout << "[SERVER] New Connection Error: " << ec.message() << '\n';
                }

                // No matter what happens, make sure the asio context still has more work.
                WaitForClientConnection();
            }
        );
    }

    /**
     * Message a client directly.
     * 
     * If the client is not connected, remove them from the server's active connections.
     * 
     * @note that this is the only way we can tell if a client has disconnected,
     * as we don't receive an explicit notification of such a fact.
    */
    void MessageClient(std::shared_ptr<connection<T>> client, const message<T>& msg) {
        if (client && client->IsConnected()) {
            client->Send(msg);

        } else {
            // If the client socket is no longer valid, assume that the client has disconnected.
            OnClientDisconnect(client);
            client.reset();
            m_activeConnections.erase(
                std::remove(m_activeConnections.begin(), m_activeConnections.end(), client), m_activeConnections.end()
            );
        }
    }

    /**
     * Message all clients, optionally ignoring a specific client.
     * 
     * If any client is not connected, they are removed from the server's active connections.
    */
    void MessageAllClients(const message<T>& msg, std::shared_ptr<connection<T>> ignoreClient = nullptr) {
        bool bInvalidClientExists = false;  // Whether at least one client has disconnected.

        for (auto& client : m_activeConnections) {
            if (client && client->IsConnected()) {
                if (client != ignoreClient) {
                    client->Send(msg);
                }

            } else {
                // If the client socket is no longer valid, assume that the client has disconnected.
                OnClientDisconnect(client);
                client.reset();
                bInvalidClientExists = true;
            }
        }

        if (bInvalidClientExists) {
            m_activeConnections.erase(
                std::remove(m_activeConnections.begin(), m_activeConnections.end(), nullptr), m_activeConnections.end()
            );
        }
    }

    /**
     * Process messages from the incoming message queue, optionally up to a maximum number.
     * 
     * @note that the default is to process the maximum unsigned integer.
    */
    void Update(size_t maxMessages = -1) {
        size_t messageCount = 0;
        while (messageCount < maxMessages && !m_qMessagesin.empty()) {
            auto msg = m_qMessagesin.pop_front();

            OnMessage(msg.m_remote, msg.m_msg);
        }
    }

protected:
    /**
     * Called when a client connected, returns whether to accept the connection.
     * Can be used to ban IP addresses or limit the number of connections.
     * 
     * Must be overridden by derived class to accept any connections.
    */
    virtual bool OnClientConnect(std::shared_ptr<connection<T>> client) = 0;

    /**
     * Called when a client appears to have disconnected.
     * Can be used to remove the user from the game state.
    */
    virtual void OnClientDisconnect(std::shared_ptr<connection<T>> client) = 0;

    /**
     * Called when a message is received from a client,
     * after we call Update to process from the queue.
    */
    virtual void OnMessage(std::shared_ptr<connection<T>> client, message<T>& msg) = 0;

    /// Thread-safe deque for incoming message packets; we own it.
    ts_deque<tagged_message<T>> m_qMessagesin;

    /// Container of active validated connections.
    std::deque<std::shared_ptr<connection<T>>> m_activeConnections;

    /// The asio context for the server.
    boost::asio::io_context m_asioContext;

    /// Thread that runs the asio context.
    std::thread m_threadContext;  

    /// Acceptor object that waits for incoming connection requests.
    boost::asio::ip::tcp::acceptor m_asioAcceptor;

    /// Clients are identified via a numeric ID, which is must simpler.
    uint32_t m_uidCounter = 100000;
};

} // namespace flash

#endif