#ifndef FLASH_TCP_SERVER_HPP
#define FLASH_TCP_SERVER_HPP

/**
 * @file server.hpp
 * 
 * Server class that wraps asio networking code using TCP.
 */

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>
#include <flash/iserver.hpp>
#include <flash/iserverext.hpp>

#include <flash/tcp/connection.hpp>

#include <iostream>

namespace flash {

namespace tcp {

/**
 * Server class that handles connections from clients.
 * 
 * Provides an interface to start and wait for connecting clients,
 * to message clients individually or all at once, and to receive
 * messages through a thread-safe queue.
 * 
 * Should be inherited for custom functionality.
 * 
 * @tparam T the message type to send and receive.
*/
template <typename T>
class server : public iserver<T>, protected iserverext<T> {
public:
    /**
     * Constructor for the server. Sets up the acceptor to listen for incoming connections.
    */
    server(uint16_t port) 
        : m_asioAcceptor(m_asioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) { }

    /**
     * Virtual destructor for the server. Stops the server if it is still running.
    */
    virtual ~server() {
        Stop();
    }

    /**
     * Start the server. It will begin listening for clients on the asio context thread.
     * 
     * @returns Whether the server started successfully.
    */
    bool Start() final {
        if (m_threadContext.joinable() && !m_asioContext.stopped()) {
            std::cout << "[SERVER] Already running!\n";
            return false;
        }

        try {
            // Issue a task to the asio context to listen for clients.
            WaitForClientConnection();

            // Start the asio context in its own thread. We issue the work before
            // to guarantee that the thread doesn't immediately stop.
            m_threadContext = std::thread([this]() { m_asioContext.run(); });

        } catch (std::exception& e) {
            // Something prohibited the server from starting, print the error.
            std::cerr << "[SERVER] Start Exception: " << e.what() << "\n";
            return false;
        }

        std::cout << "[SERVER] Started on port " << m_asioAcceptor.local_endpoint().port() << "\n";
        return true;
    }

    /**
     * Stop the server. Request the context to close, and then wait on the thread to join it.
    */
    void Stop() final {
        // Request the context to close.
        m_asioContext.stop();

        // Wait for the context thread to finish.
        if (m_threadContext.joinable()) m_threadContext.join();

        std::cout << "[SERVER] Stopped!\n";

        m_asioContext.reset();  // In case you want to reuse this Server object.
    }

    /**
     * Message a client directly.
     * 
     * If the client is not connected, remove them from the server's active connections.
     * 
     * @note that this is the only way we can tell if a client has disconnected,
     * as we don't receive an explicit notification of such a fact.
    */
    void MessageClient(UserId clientId, message<T>&& msg) final {
        // Find the client in the active connections.
        auto client = m_activeConnections.find(clientId);
        
        // If the client is found and connected, send a message
        if (client != m_activeConnections.end() && client->second && client->second->IsConnected()) {
            client->second->Send(std::move(msg));

        } else {
            // If the client socket is no longer valid, assume that the client has disconnected.
            m_activeConnections.erase(client);
            OnClientDisconnect(clientId);
        }
    }

    /**
     * Message all clients, optionally ignoring a specific client.
     * 
     * If any client is not connected, they are removed from the server's active connections.
    */
    void MessageAllClients(message<T>&& msg, UserId ignoreClient = INVALID_USER_ID) final {
        std::vector<UserId> disconnectedClients;

        for (auto& [id, client] : m_activeConnections) {
            if (id == ignoreClient) continue;

            if (client && client->IsConnected()) {
                message<T> msgCopy = msg;
                client->Send(std::move(msgCopy));

            } else {
                // If the client socket is no longer valid, assume that the client has disconnected.
                disconnectedClients.push_back(id);
            }
        }

        for (auto id : disconnectedClients) {
            m_activeConnections.erase(id);
        }

        for (auto id : disconnectedClients) {
            OnClientDisconnect(id);
        }
    }

    /**
     * Process messages from the incoming message queue, optionally up to a maximum number.
     * 
     * @note that the default is to process the maximum unsigned integer.
     * 
     * @param maxMessages the maximum number of messages to process.
     * @param wait whether to wait for messages to arrive by sleeping.
    */
    void Update(size_t maxMessages = -1, bool wait = false) final {
        // Wait until something is deposited into the queue.
        if (wait) m_qMessagesin.wait();

        size_t messageCount = 0;
        while (messageCount < maxMessages && !m_qMessagesin.empty()) {
            // A tagged message has arrived, so we process it.
            auto taggedMsg = m_qMessagesin.pop_front();
            OnMessage(taggedMsg.m_remote, std::move(taggedMsg.m_msg));
            ++messageCount;
        }
    }

protected:
    /**
     * Called when a client connected, returns whether to accept the connection.
     * Can be used to ban IP addresses or limit the number of connections.
     * 
     * Must be overridden by derived class to accept any connections.
    */
    bool OnClientConnect(const boost::asio::ip::address& address) override = 0;

    /**
     * Called when a client is validated by the simple scramble check.
     * 
     * Must be overridden by derived class to handle validation.
    */
    void OnClientValidate(UserId clientId) override = 0;

    /**
     * Called when a client appears to have disconnected.
     * Can be used to remove the user from the game state.
     * 
     * Must be overriden by derived class to handle disconnections.
    */
    void OnClientDisconnect(UserId clientId) override = 0;

    /**
     * Called when a message is received from a client,
     * after we call Update to process from the queue.
     * 
     * Must be overriden by derived class to handle messages.
    */
    void OnMessage(UserId clientId, message<T>&& msg) override = 0;

    /// Thread-safe deque for incoming message packets; we own it.
    ts_deque<tagged_message<T>> m_qMessagesin;

    /// Container of active validated connections.
    std::unordered_map<UserId, std::unique_ptr<connection<T>>> m_activeConnections;

    /// The asio context for the server.
    boost::asio::io_context m_asioContext;

    /// Thread that runs the asio context.
    std::thread m_threadContext; 

    /// Acceptor object that waits for incoming connection requests.
    boost::asio::ip::tcp::acceptor m_asioAcceptor;

    /// Clients are identified via a numeric ID, which is must simpler.
    /// Six digits for pretty printing with "SERVER".
    UserId m_uidCounter = 100000;

    friend class connection<T>;

private:
    /**
     * Asynchronous task for the asio context thread, waiting for a client to connect.
     * 
     * This task should be constantly running on the server, even if no clients are around.
    */
    void WaitForClientConnection() {
        m_asioAcceptor.async_accept(
            [this](std::error_code ec, boost::asio::ip::tcp::socket socket) {
                if (!ec) {
                    std::cout << "[SERVER] New Connection from IP: " << socket.remote_endpoint() << "\n";

                    // Make a new connection.
                    std::unique_ptr<connection<T>> new_connection = std::make_unique<connection<T>>(
                        connection<T>::owner::server,
                        m_asioContext,     // Provide the connection with the surrounding asio context.
                        std::move(socket), // Move the new socket into the connection.
                        m_qMessagesin      // Reference to the server's incoming message queue.
                    );

                    // Give the custom server a chance to deny connection by overriding OnClientConnect.
                    if (OnClientConnect(new_connection->GetSocket().remote_endpoint().address())) {
                        // Assign a unique ID to this connection.
                        UserId newId = m_uidCounter++;

                        // Transfer ownership of the new connection to the server.
                        m_activeConnections.emplace(newId, std::move(new_connection));

                        // Tell the connection to connect to the client.
                        m_activeConnections.at(newId)->ConnectToClient(newId, this);

                        std::cout << "[" << newId << "] Connection Approved\n";

                    } else {
                        std::cout << "[------] Connection Denied\n";
                    }

                } else {
                    std::cout << "[SERVER] New Connection Error: " << ec.message() << "\n";
                }

                // No matter what happens, make sure the asio context still has more work.
                WaitForClientConnection();
            }
        );
    }
};

} // namespace tcp

} // namespace flash

#endif