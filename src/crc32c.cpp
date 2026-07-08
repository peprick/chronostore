#include "internal/crc32c.hpp"

#include <cstddef>

namespace chronostore::internal {
namespace {

constexpr std::uint32_t initial_value = 0xFFFFFFFFU;
constexpr std::uint32_t final_xor_value = 0xFFFFFFFFU;
constexpr std::uint32_t reflected_polynomial = 0x82F63B78U;
constexpr std::size_t bits_per_byte = 8;

} // namespace

std::uint32_t crc32c(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t checksum = initial_value;

    for (std::uint8_t byte : bytes) {
        checksum ^= static_cast<std::uint32_t>(byte);

        for (std::size_t bit = 0; bit < bits_per_byte; ++bit) {
            const bool lowest_bit_is_set = (checksum & 1U) != 0U;

            checksum >>= 1U;

            if (lowest_bit_is_set) {
                checksum ^= reflected_polynomial;
            }
        }
    }

    return checksum ^ final_xor_value;
}

} // namespace chronostore::internal
