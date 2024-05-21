#ifndef FLASH_ISERVER_HPP
#define FLASH_ISERVER_HPP

/**
 * @file iserver.hpp
*/

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

namespace flash {

/**
 * Server interface that allows you to message clients and initiate updates.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
 *         Should have an underlying type of uint32_t.
*/
template <typename T>
class iserver {
public:
    virtual ~iserver() { }

    virtual bool Start() = 0;
    virtual void Stop() = 0;

    virtual void MessageClient(
        UserId clientId, message<T>&& msg) = 0;
    
    virtual void MessageAllClients(
        message<T>&& msg, UserId ignoreId = INVALID_USER_ID) = 0;

    virtual void Update(size_t maxMessages = -1, bool wait = true) = 0;
};

} // namespace flash

#endif