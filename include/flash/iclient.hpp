#ifndef FLASH_ICLIENT_HPP
#define FLASH_ICLIENT_HPP

/**
 * @file iclient.hpp
*/

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

namespace flash {

/**
 * Client interface that allows you to send message and access a thread-safe queue of incoming messages.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
 *         Should have an underlying type of uint32_t.
*/
template <typename T>
class iclient {
public:
    virtual ~iclient() { }

    virtual bool Connect(
        const std::string& host, const uint16_t port) = 0;

    virtual void Disconnect() = 0;
    virtual bool IsConnected() = 0;

    virtual void Send(message<T>&& msg) = 0;

    virtual ts_deque<tagged_message<T>>& Incoming() = 0;
};

} // namespace flash

#endif