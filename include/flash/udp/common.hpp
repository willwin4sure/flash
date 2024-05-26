#ifndef FLASH_UDP_COMMON_HPP
#define FLASH_UDP_COMMON_HPP

#include <cstdint>

namespace flash {

namespace udp {

constexpr uint32_t MAX_MESSAGE_SIZE_IN_BYTES = 64000;
constexpr uint64_t CONNECTION_REQUEST_MAGIC_NUMBER = 0x26E55500;

} // namespace udp

} // namespace flash

#endif