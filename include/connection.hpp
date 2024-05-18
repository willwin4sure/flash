#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "message.hpp"

#include <boost/asio.hpp>

#include <memory>

namespace flash {
    template <typename T>
    class connection : public std::enable_shared_from_this<connection<T>> {
    public:
        connection() {}
        virtual ~connection() {}

        bool ConnectToServer();
        bool Disconnect();
        bool IsConnected() const;

        bool Send(const message<T>& msg);

    protected:
        /// Each connection has a unique socket that is connected to a remote; we own it.
        boost::asio::ip::tcp::socket m_socket;

        /// The asio context is shared with all other connections.
        boost::asio::io_context& m_asioContext;

        /// Queue holding messages to be sent to the remote; we own it.
        ts_deque<message<T>> m_qMessagesOut;

        /// Queue holding messages received from the remote side, owned by the client or server.
        ts_deque<tagged_message<T>>& m_qMessagesIn;
    };
}

#endif