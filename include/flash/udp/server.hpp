#ifndef UDP_SERVER_HPP
#define UDP_SERVER_HPP

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

#include <boost/asio.hpp>

#include <iostream>
#include <sstream>

namespace flash {

namespace udp {

template <typename T>
class server {
public:
    server(uint16_t port)
        : m_socket { m_asioContext, boost::asio::ip::udp::endpoint { boost::asio::ip::udp::v4(), port } } {

        m_port = port;
    }

    bool Start() {
        if (m_threadContext.joinable() && !m_asioContext.stopped()) {
            std::cout << "[SERVER] Already running!\n";
            return false;
        }

        try {
            WaitForMessages();

            m_threadContext = std::thread([this]() { m_asioContext.run(); });

        } catch (std::exception& e) {
            std::cerr << "[SERVER] Start Exception: " << e.what() << "\n";
            return false;
        }

        std::cout << "[SERVER] Started on port " << m_port << "\n";
        return true;
    }

    void Stop() {
        m_asioContext.stop();

        if (m_threadContext.joinable()) {
            m_threadContext.join();
        }

        std::cout << "[SERVER] Stopped!\n";

        m_asioContext.reset();
    }

private:
    // Port to listen on.
    uint16_t m_port;

    // The asio context for the server.
    boost::asio::io_context m_asioContext;

    // Socket to listen for incoming messages.
    boost::asio::ip::udp::socket m_socket;

    /// Thread that runs the asio context.
    std::thread m_threadContext;
};

} // namespace udp

} // namespace flash

#endif