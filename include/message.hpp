#ifndef MESSAGE_H
#define MESSAGE_H

/**
 * @file message.hpp
 * 
 * Structs for messages on a network connection.
*/

#include <cstdint>
#include <iostream>

namespace flash {

/**
 * Header that is sent at the start of every message, with a fixed size.
 * 
 * @tparam T the type of the message id. Should be an enum class
 *         with underlying type of uint32_t.
 * 
 * @property id the id of the message.
 * @property size the size of the message in bytes, including the header.
*/
template <typename T>
struct Header {
    T id {};
    uint32_t size { 0 };
};


/**
 * Message class that is used to send and receive messages over a network connection.
 * 
 * Supports pushing and popping fundamental data types with the `<<` and `>>` operators.
 * 
 * @tparam T the type of the message id. Should be an enum class
 *         with underlying type of uint32_t.
 * 
 * @property header the header of the message, containing the id and size.
 * @property body the body of the message, containing the data.
*/
template <typename T>
class Message {
public:
    /**
     * Constructs an empty message with the given id.
    */
    Message(T id) : header { id, 0 } {}

    /**
     * Get the size of the entire message in bytes.
    */
    size_t size() const { return sizeof(Header<T>) + body.size(); }

    /**
     * Gets a const reference to the header of the message.
    */
    const Header<T>& get_header() const { return header; }

    /**
     * Gets a const reference to the full message body as a vector of bytes.
    */
    const std::vector<uint8_t>& get_body() const { return body; }

    /**
     * Allow easy printing of the message using `std::cout`.
    */
    friend std::ostream& operator<<(std::ostream& os, const Message<T>& msg) {
        os << "ID: " << static_cast<int>(msg.header.id) << " Size: " << msg.header.size;
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
     * one option is to just use string representations of the data.
    */
    template <typename U>
    friend Message<T>& operator<<(Message<T>& msg, const U& data) {
        static_assert(std::is_standard_layout<U>::value,
            "Data is too complex to be pushed into message.");

        // Resize the message body to fit the new data
        size_t i = msg.body.size();
        msg.body.resize(msg.body.size() + sizeof(U));

        // Copy the data into the message body
        std::memcpy(msg.body.data() + i, &data, sizeof(U));
        msg.header.size = msg.size();

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
    friend Message<T>& operator>>(Message<T>& msg, U& data) {
        static_assert(std::is_standard_layout<U>::value,
            "Data is too complex to be popped from message.");

        // Get the size of the data being popped
        size_t i = msg.body.size() - sizeof(U);

        // Copy the data from the message body into the variable
        std::memcpy(&data, msg.body.data() + i, sizeof(U));

        // Shrink the message body to remove the data
        msg.body.resize(i);
        msg.header.size = msg.size();

        return msg;
    }

private:
    Header<T> header;
    std::vector<uint8_t> body;
};

} // namespace flash

#endif