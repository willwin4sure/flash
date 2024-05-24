#ifndef FLASH_ISERVEREXT_HPP
#define FLASH_ISERVEREXT_HPP

/**
 * @file iserverext.hpp
*/

#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

#include <boost/asio.hpp>

namespace flash {

/**
 * Contains the extensible components of a server that allows custom
 * functionality in derived classes.
 * 
 * Should be inherited from using the `protected` keyword, as this
 * is not a public-facing interface.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
 *         Should have an underlying type of uint32_t.
*/
template <typename T>
class iserverext {
public:
    virtual bool OnClientConnect(const boost::asio::ip::address& address) = 0;
    virtual void OnClientValidate(UserId clientId) = 0;
    virtual void OnClientDisconnect(UserId clientId) = 0;
    virtual void OnMessage(UserId clientId, message<T>&& msg) = 0;
};

} // namespace flash

#endif