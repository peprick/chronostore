#ifndef CHRONOSTORE_INTERNAL_FILE_IO_HPP
#define CHRONOSTORE_INTERNAL_FILE_IO_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace chronostore::internal {

[[nodiscard]] std::uint64_t file_size_bytes(const std::filesystem::path& path);

[[nodiscard]] std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path,
                                                        std::uint64_t offset, std::size_t count);

void atomic_write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_FILE_IO_HPP
