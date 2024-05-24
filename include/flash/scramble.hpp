#ifndef FLASH_SCRAMBLE_HPP
#define FLASH_SCRAMBLE_HPP

#include <cstdint>

namespace flash {

/**
 * Mixes 64 bits into 32 bits with improved entropy.
*/
uint32_t MixBits(uint64_t x) {
    x = x ^ 0xA0B1C2D3;
    uint32_t xor_shifted = ((x >> 18u) ^ x) >> 27u;
    uint32_t rot = x >> 59u;
    uint32_t res = (xor_shifted >> rot) | (xor_shifted << ((-rot) & 31));
    return res ^ 0x12345678;
}

/**
 * Scrambles the input using a rather random function.
*/
uint64_t Scramble(uint64_t input) {
    static constexpr uint64_t LARGE_PRIME = 6364136223846793005ULL;
    static constexpr uint64_t OFFSET      = 000'001'000;  // Encodes the version of the protocol.

    return static_cast<uint64_t>(MixBits(static_cast<uint64_t>(MixBits(input)) * LARGE_PRIME + OFFSET)) * LARGE_PRIME + OFFSET;
}

} // namespace flash

#endif