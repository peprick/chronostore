#ifndef CHRONOSTORE_INTERNAL_CRC32C_HPP
#define CHRONOSTORE_INTERNAL_CRC32C_HPP

#include <cstdint>
#include <span>

namespace chronostore::internal {

[[nodiscard]] std::uint32_t crc32c(std::span<const std::uint8_t> bytes) noexcept;

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_CRC32C_HPP
