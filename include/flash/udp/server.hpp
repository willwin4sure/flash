#ifndef FLASH_UDP_SERVER_HPP
#define FLASH_UDP_SERVER_HPP

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

#include <flash/iserver.hpp>
#include <flash/iserverext.hpp>
#include <flash/scramble.hpp>

#include <flash/udp/common.hpp>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace flash {

namespace udp {

struct User {
    boost::asio::ip::udp::endpoint m_endpoint;
    std::chrono::steady_clock::time_point m_lastMessageTime;  // Timestamp of the last message received.

    bool m_validated { false };       // Whether the user has passed the basic validation handshake.
    uint64_t m_handshake { 0 };       // The input handshake value.
    uint64_t m_handshakeCheck { 0 };  // The correct output handshake value.
};

template <typename T>
class server : public iserver<T>, protected iserverext<T> {
public:
    server(uint16_t port, uint32_t serverTimeout = 5000)
        : m_socket { m_asioContext, boost::asio::ip::udp::endpoint { boost::asio::ip::udp::v4(), port } },
          m_serverTimeout { serverTimeout } {

        m_tempBufferIn.resize(MAX_MESSAGE_SIZE_IN_BYTES);
    }

    virtual ~server() { }

    bool Start() final {
        if (m_threadContext.joinable() && !m_asioContext.stopped()) {
            std::cout << "[SERVER] Already running!\n";
            return false;
        }

        try {
            WaitForMessages();

            m_threadContext = std::thread([this]() { m_asioContext.run(); });

        } catch (std::exception& e) {
            std::stringstream ss;
            ss << "[SERVER] Exception: " << e.what() << "\n";
            std::cout << ss.str();

            return false;
        }

        std::stringstream ss;
        ss << "[SERVER] Started on port " << m_socket.local_endpoint().port() << "\n";
        std::cout << ss.str();

        return true;
    }

    void Stop() final {
        boost::asio::post(m_asioContext, [this]() { m_asioContext.stop(); });
        
        if (m_threadContext.joinable()) {
            m_threadContext.join();
        }

        std::cout << "[SERVER] Stopped!\n";

        m_asioContext.reset();
    }

    void MessageClient(UserId clientId, message<T>&& msg) final {
        Send(clientId, std::move(msg));
    }

    void MessageAllClients(message<T>&& msg, UserId ignoreId = INVALID_USER_ID) final {
        for (auto& user : m_userIdToUser) {
            if (user.first != ignoreId) {
                Send(user.first, std::move(msg));
            }
        }
    }

    void Update(size_t maxMessages = -1, bool wait = false) final {
        if (wait) m_qMessagesIn.wait();

        size_t messageCount = 0;
        while (messageCount < maxMessages && !m_qMessagesIn.empty()) {
            auto taggedMsg = m_qMessagesIn.pop_front();
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

    ts_deque<tagged_message<T>> m_qMessagesIn;   // Queue of incoming messages.
    ts_deque<tagged_message<T>> m_qMessagesOut;  // Queue of outgoing messages.

    boost::asio::io_context m_asioContext;  // The asio context for the server.
    boost::asio::ip::udp::socket m_socket;  // Socket to listen for incoming messages.
    std::thread m_threadContext;            // Thread that runs the asio context.

    boost::asio::ip::udp::endpoint m_remoteEndpoint;  // Holds the remote endpoint that last sent a message.

    std::vector<uint8_t> m_tempBufferIn;    // Buffer to store incoming messages.
    std::vector<uint8_t> m_tempBufferOut;   // Buffer to store outgoing messages.

    uint64_t m_tempHandshakeOut;            // Temporary handshake value for sending.

    UserId m_uidCounter = 100000;  // Used to assign unique 6-digit IDs to clients.
    uint32_t m_serverTimeout;      // Disconnection timeout for clients in ms.

    /// Maps remote endpoints to user IDs.
    std::unordered_map<boost::asio::ip::udp::endpoint, UserId> m_endpointToUserId;
    
    /// Maps user IDs to user data.
    std::unordered_map<UserId, User> m_userIdToUser;

private:
    void HandleNewConnection(std::size_t length) {
        // Message is not correct size, ignore.
        if (length != sizeof(uint64_t)) return;

        // Read the magic number.
        uint64_t magicNumber;
        std::memcpy(&magicNumber, m_tempBufferIn.data(), sizeof(uint64_t));
        magicNumber = boost::endian::big_to_native(magicNumber);

        // Magic number does not match, ignore.
        if (magicNumber != CONNECTION_REQUEST_MAGIC_NUMBER) return;

        // Give the custom server a chance to deny connection by overriding OnClientConnect.
        if (OnClientConnect(m_remoteEndpoint.address())) {
            UserId newId = m_uidCounter++;
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

            // Generate validation data
            uint64_t handshake = Scramble(uint64_t(now.time_since_epoch().count()));
            uint64_t handshakeCheck = Scramble(handshake);

            // Assign the user to the endpoint.
            m_endpointToUserId[m_remoteEndpoint] = newId;
            m_userIdToUser[newId] = User { m_remoteEndpoint, now, false, handshake, handshakeCheck };

            // Send the validation handshake.
            SendValidation(newId);

            std::stringstream ss;
            ss << "[" << newId << "] Connection Approved\n";
            std::cout << ss.str();

        } else {
            std::cout << "[------] Connection Denied\n";
        }
    }

    void HandleValidation(UserId userId, std::size_t length) {
        if (length != sizeof(uint64_t)) {
            // Message is not correct size, kill the user.
            std::stringstream ss;
            ss << "[" << userId << "] Client Handshake Failed.\n";
            std::cout << ss.str();

            m_endpointToUserId.erase(m_remoteEndpoint);
            m_userIdToUser.erase(userId);

            return;
        }

        uint64_t handshakeIn;
        std::memcpy(&handshakeIn, m_tempBufferIn.data(), sizeof(uint64_t));
        handshakeIn = boost::endian::big_to_native(handshakeIn);

        if (handshakeIn != m_userIdToUser[userId].m_handshakeCheck) {
            // Handshake does not match, kill the user.
            std::stringstream ss;
            ss << "[" << userId << "] Client Handshake Failed.\n";
            std::cout << ss.str();

            m_endpointToUserId.erase(m_remoteEndpoint);
            m_userIdToUser.erase(userId);

            return;
        }

        m_userIdToUser[userId].m_validated = true;
        m_userIdToUser[userId].m_lastMessageTime = std::chrono::steady_clock::now();

        std::stringstream ss;
        ss << "[" << userId << "] Client Validated.\n";
        std::cout << ss.str();

        OnClientValidate(userId);
    }

    void ProcessMessage(UserId userId, std::size_t length) {
        // Message is too short to be in the canonical format, ignore
        if (length < sizeof(header<T>)) return;

        message<T> msg { static_cast<T>(0) };
        std::memcpy(&msg.get_header(), m_tempBufferIn.data(), sizeof(header<T>));
        msg.get_header().m_size = boost::endian::big_to_native(msg.get_header().m_size);

        // Size of message body does not match the size in the header, ignore
        if (length - sizeof(header<T>) != msg.get_header().m_size) return;

        msg.get_body().resize(msg.get_header().m_size);
        std::memcpy(msg.get_body().data(), m_tempBufferIn.data() + sizeof(header<T>), msg.get_header().m_size);

        m_userIdToUser[userId].m_lastMessageTime = std::chrono::steady_clock::now();

        // Push the message to the incoming queue.
        m_qMessagesIn.push_back(tagged_message<T> { userId, std::move(msg) });
    }
    
    void WaitForMessages() {
        m_socket.async_receive_from(
            boost::asio::buffer(m_tempBufferIn.data(), m_tempBufferIn.size()), m_remoteEndpoint,
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    CleanupUsers();

                    // Handles the case that the endpoint is new
                    if (m_endpointToUserId.find(m_remoteEndpoint) == m_endpointToUserId.end()) {
                        HandleNewConnection(length);

                        WaitForMessages();
                        return;
                    }

                    // The endpoint is known and has been assigned an ID.
                    UserId userId = m_endpointToUserId[m_remoteEndpoint];

                    if (m_userIdToUser[userId].m_validated == false) {
                        HandleValidation(userId, length);

                        WaitForMessages();
                        return;
                    }

                    // The user is validated and we will process their messages.
                    ProcessMessage(userId, length);

                } else {
                    std::stringstream ss;
                    ss << "[SERVER] Error receiving message: " << ec.message() << "\n";
                    std::cout << ss.str();
                }

                WaitForMessages();
            }
        );
    }

    void Send(UserId userId, message<T>&& msg) {
        // Message is too long, reject.
        if (msg.size() > MAX_MESSAGE_SIZE_IN_BYTES) {
            assert(false);
            return;
        }

        boost::asio::post(
            m_asioContext,

            // Black magic generalized lambda capture from
            // https://stackoverflow.com/questions/8640393/move-capture-in-lambda

            [this, userId, msg = std::move(msg)] () mutable {
                bool writing = !m_qMessagesOut.empty();
                msg.get_header().m_size = boost::endian::native_to_big(msg.get_header().m_size);
                m_qMessagesOut.push_back({ userId, std::move(msg) });

                if (!writing) {
                    SendMessages();
                }
            }
        );
    }

    void SendValidation(UserId userId) {
        m_tempHandshakeOut = boost::endian::native_to_big(m_userIdToUser[userId].m_handshake);

        m_socket.async_send_to(
            boost::asio::buffer(&m_tempHandshakeOut, sizeof(uint64_t)), m_userIdToUser[userId].m_endpoint,
            [this](std::error_code ec, std::size_t length) {
                if (ec) {
                    std::stringstream ss;
                    ss << "[SERVER] Error sending validation: " << ec.message() << "\n";
                    std::cout << ss.str();
                }
            }
        );
    }

    void SendMessages() {
        if (m_qMessagesOut.empty()) return;

        CleanupUsers();

        // Make sure that the user for the message is valid.
        while (m_userIdToUser.find(m_qMessagesOut.front().m_remote) == m_userIdToUser.end()) {
            m_qMessagesOut.pop_front();
            if (m_qMessagesOut.empty()) return;
        }

        tagged_message<T> taggedMsg = m_qMessagesOut.front();
        UserId userId = taggedMsg.m_remote;
        message<T>& msg = taggedMsg.m_msg;

        m_tempBufferOut.resize(msg.size());

        std::memcpy(m_tempBufferOut.data(), &msg.get_header(), sizeof(header<T>));
        std::memcpy(m_tempBufferOut.data() + sizeof(header<T>), msg.get_body().data(), msg.get_body().size());

        m_socket.async_send_to(
            boost::asio::buffer(m_tempBufferOut.data(), m_tempBufferOut.size()), m_userIdToUser[userId].m_endpoint,
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    m_qMessagesOut.pop_front();

                    if (!m_qMessagesOut.empty()) {
                        SendMessages();
                    }

                } else {
                    std::stringstream ss;
                    ss << "[SERVER] Error sending message: " << ec.message() << "\n";
                    std::cout << ss.str();
                }
            }
        );
    }

    void CleanupUsers() {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

        std::vector<UserId> disconnectedUsers;

        for (auto it = m_userIdToUser.begin(); it != m_userIdToUser.end(); ++it) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.m_lastMessageTime).count() > m_serverTimeout) {
                disconnectedUsers.push_back(it->first);
            }
        }

        for (auto userId : disconnectedUsers) {
            std::stringstream ss;
            ss << "[" << userId << "] Client Timed Out.\n";
            std::cout << ss.str();

            m_endpointToUserId.erase(m_userIdToUser[userId].m_endpoint);
            m_userIdToUser.erase(userId);
        }

        for (auto userId : disconnectedUsers) {
            OnClientDisconnect(userId);
        }
    }
};

} // namespace udp

} // namespace flash

#endif