#ifndef FLASH_MESSAGE_HPP
#define FLASH_MESSAGE_HPP

/**
 * @file message.hpp
 * 
 * Structs for messages on a network connection, including a header and body.
 * Provides an interface for pushing and popping fundamental data types.
*/

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace flash {

/// Type of the user ID. Server ID is 0, Client IDs are positive integers, e.g. starting at 100000.
using UserId = int32_t;

constexpr UserId INVALID_USER_ID = -1;  // Represents some unassigned user ID.
constexpr UserId SERVER_USER_ID = 0;    // User ID of the unique server.


/**
 * Header struct that is sent at the start of every message, with a fixed size.
 * Contains the type of the message and the size of the message body.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
 *         Should have an underlying type of uint32_t.
*/
template <typename T>
struct header {
    T m_type {};
    uint32_t m_size { 0 };  // Size of the message body associated with this header.
};


/**
 * Message struct that is used to send and receive messages over a network connection.
 * Contains both the header of the message and the body of the message.
 * 
 * Supports pushing and popping fundamental data types with the `<<` and `>>` operators.
 * 
 * @warning For types larger than a byte, does not handle endianness, so only use byte-sized
 * types if your machines differ in endianness. Most modern machines are little-endian.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
 *         Should have an underlying type of uint32_t.
*/
template <typename T>
struct message {
    /**
     * Constructs an empty message with the given type.
    */
    message(T type) : m_header { type, 0 } { }

    /**
     * @returns The size of the entire message in bytes.
    */
    size_t size() const { return sizeof(header<T>) + m_body.size(); }

    /**
     * @returns A const reference to the header of the message.
    */
    const header<T>& get_header() const { return m_header; }

    /**
     * @returns A const reference to the body of the message.
    */
    const std::vector<uint8_t>& get_body() const { return m_body; }

    /**
     * @returns A reference to the header of the message.
    */
    header<T>& get_header() { return m_header; }

    /**
     * @returns A reference to the body of the message.
    */
    std::vector<uint8_t>& get_body() { return m_body; }
    
    /**
     * Allow easy printing of the message using `std::cout`.
    */
    friend std::ostream& operator<<(std::ostream& os, const message<T>& msg) {
        os << "Type: " << static_cast<int>(msg.m_header.m_type) << " "
           << "Size: " << msg.size();
        return os;
    }

    /**
     * Push fundamental data types into the message body,
     * such as `int`, `float`, `struct`, and arrays.
     * 
     * Operates like a stack, pushing data to the end of the message body.
     * 
     * Can be chained together, e.g. `msg << data1 << data2 << data3;`.
     * 
     * @tparam U the type of the data being pushed.
     * 
     * @warning Doesn't handle endianness, so may not work across different architectures.
     * If you care about this, then serialize the data yourself before pushing it;
     * one option is to just use string representations of the data. Or just only
     * use data with byte-sized types.
    */
    template <typename U>
    friend message<T>& operator<<(message<T>& msg, const U& data) {
        static_assert(std::is_standard_layout<U>::value,
            "Data is too complex to be pushed into message.");

        // Resize the message body to fit the new data.
        size_t sz = msg.m_body.size();
        msg.m_body.resize(sz + sizeof(U));

        // Copy the data into the message body from the data.
        std::memcpy(msg.m_body.data() + sz, &data, sizeof(U));
        msg.m_header.m_size = msg.m_body.size();

        return msg;
    }

    /**
     * Pop fundamental data types from the message body, such as `int`, `float`, etc.
     * 
     * Operates like a stack, popping data from the end of the message body.
     * 
     * Can be chained together, e.g. `msg >> data3 >> data2 >> data1;`.
     * 
     * @tparam U the type of the data being popped.
     * 
     * @warning Doesn't handle endianness, so may not work across different architectures.
    */
    template <typename U>
    friend message<T>& operator>>(message<T>& msg, U& data) {
        static_assert(std::is_standard_layout<U>::value,
            "Data is too complex to be popped from message.");

        // Remove the size of the data being popped.
        size_t sz = msg.m_body.size() - sizeof(U);

        // Copy the data into the variable from the message body.
        std::memcpy(&data, msg.m_body.data() + sz, sizeof(U));

        // Shrink the message body to remove the data.
        msg.m_body.resize(sz);
        msg.m_header.m_size = msg.m_body.size();

        return msg;
    }

private:
    header<T> m_header;
    std::vector<uint8_t> m_body;  // Body of message as a vector of bytes.
};


/**
 * Wrapper around a message that contains the user ID of the sender.
 * 
 * @tparam T an enum class containing possible types of messages to be sent.
 *         Should have an underlying type of uint32_t.
*/
template <typename T>
struct tagged_message {
    /**
     * Constructs a tagged message with the given message and sender ID.
    */
    tagged_message(UserId remote, message<T>&& msg)
        : m_remote { remote }, m_msg { std::move(msg) } { }

    friend std::ostream& operator<<(std::ostream& os, const tagged_message<T>& tagged_msg) {
        os << "Remote: " << tagged_msg.m_remote << " "
           << "Message: " << tagged_msg.m_msg;
        return os;
    }

    UserId m_remote { INVALID_USER_ID };  // ID of the remote user.
    message<T> m_msg;
};

} // namespace flash

#endif