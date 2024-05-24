#ifndef FLASH_UDP_COMMON_HPP
#define FLASH_UDP_COMMON_HPP

#include <cstdint>

namespace flash {

namespace udp {

constexpr uint32_t MAX_MESSAGE_SIZE = 64000;                   // Maximum total size of a message in bytes.
constexpr uint64_t CONNECTION_REQUEST_MAGIC_NUM = 0x26E55500;  // Magic number to identify connection requests.

} // namespace udp

} // namespace flash

#endif