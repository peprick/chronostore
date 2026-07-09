#include "internal/wal_recovery.hpp"
#include "internal/wal_file.hpp"
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace chronostore::internal {
namespace {

constexpr std::size_t read_chunk_size = 64U * 1024U;

const char* decode_error_name(WalDecodeError error) noexcept {
    switch (error) {
    case WalDecodeError::none:
        return "no error";
    case WalDecodeError::incomplete_record:
        return "incomplete record";
    case WalDecodeError::invalid_magic:
        return "invalid magic";
    case WalDecodeError::unsupported_version:
        return "unsupported version";
    case WalDecodeError::unsupported_record_type:
        return "unsupported record type";
    case WalDecodeError::invalid_length:
        return "invalid length";
    case WalDecodeError::checksum_mismatch:
        return "checksum mismatch";
    case WalDecodeError::invalid_payload:
        return "invalid payload";
    }

    return "unknown decode error";
}

std::string make_format_error_message(std::uint64_t offset, WalDecodeError error) {
    return "invalid WAL record at byte " + std::to_string(offset) + ": " + decode_error_name(error);
}

[[noreturn]] void throw_read_error(const std::filesystem::path& path) {
    const std::string message = "failed to read WAL: " + path.string();

    if (errno != 0) {
        throw std::system_error(errno, std::generic_category(), message);
    }

    throw std::system_error(std::make_error_code(std::errc::io_error), message);
}

bool read_next_chunk(std::ifstream& input, std::vector<std::uint8_t>& buffer,
                     const std::filesystem::path& path) {
    std::array<char, read_chunk_size> chunk{};

    errno = 0;

    input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));

    if (input.bad() || (input.fail() && !input.eof())) {
        throw_read_error(path);
    }

    const std::size_t bytes_read = static_cast<std::size_t>(input.gcount());

    for (std::size_t index = 0; index < bytes_read; ++index) {
        buffer.push_back(std::bit_cast<std::uint8_t>(chunk[index]));
    }

    return input.eof();
}

void remove_consumed_prefix(std::vector<std::uint8_t>& buffer, std::size_t consumed) {
    if (consumed == 0) {
        return;
    }

    if (consumed == buffer.size()) {
        buffer.clear();
        return;
    }

    using DifferenceType = std::vector<std::uint8_t>::difference_type;

    buffer.erase(buffer.begin(), buffer.begin() + static_cast<DifferenceType>(consumed));
}

} // namespace

WalFormatError::WalFormatError(std::uint64_t offset, WalDecodeError decode_error)
    : std::runtime_error(make_format_error_message(offset, decode_error)), offset_(offset),
      decode_error_(decode_error) {}

std::uint64_t WalFormatError::offset() const noexcept {
    return offset_;
}

WalDecodeError WalFormatError::decode_error() const noexcept {
    return decode_error_;
}

WalReplayResult replay_wal(const std::filesystem::path& path, const WalReplayCallback& replay) {
    if (!replay) {
        throw std::invalid_argument("WAL replay callback cannot be empty");
    }

    errno = 0;

    std::ifstream input(path, std::ios::binary);

    if (!input.is_open()) {
        throw_read_error(path);
    }

    WalReplayResult result;

    std::vector<std::uint8_t> buffer;
    buffer.reserve(read_chunk_size);

    std::size_t consumed = 0;
    bool reached_end = false;

    while (true) {
        if (consumed < buffer.size()) {
            const std::span<const std::uint8_t> available{buffer.data() + consumed,
                                                          buffer.size() - consumed};

            const WalDecodeResult decoded = decode_wal_record(available);

            if (decoded.success()) {
                if (decoded.bytes_consumed == 0 || decoded.bytes_consumed > available.size()) {
                    throw std::logic_error("WAL decoder reported an invalid size");
                }

                replay(decoded.record.value());

                consumed += decoded.bytes_consumed;
                result.valid_bytes += static_cast<std::uint64_t>(decoded.bytes_consumed);
                ++result.records_replayed;

                if (consumed == buffer.size()) {
                    buffer.clear();
                    consumed = 0;
                }

                continue;
            }

            if (decoded.error != WalDecodeError::incomplete_record) {
                throw WalFormatError(result.valid_bytes, decoded.error);
            }

            if (reached_end) {
                result.incomplete_tail_bytes = static_cast<std::uint64_t>(buffer.size() - consumed);

                return result;
            }
        } else if (reached_end) {
            return result;
        }

        remove_consumed_prefix(buffer, consumed);

        consumed = 0;

        reached_end = read_next_chunk(input, buffer, path);
    }
}

void repair_wal_tail(const std::filesystem::path& path, const WalReplayResult& replay_result) {
    if (!replay_result.had_incomplete_tail()) {
        return;
    }

    const std::uint64_t maximum_size = std::numeric_limits<std::uint64_t>::max();

    if (replay_result.incomplete_tail_bytes > maximum_size - replay_result.valid_bytes) {
        throw std::overflow_error("recovered WAL size would overflow");
    }

    const std::uint64_t expected_file_size =
        replay_result.valid_bytes + replay_result.incomplete_tail_bytes;

    std::error_code file_size_error;

    const std::uintmax_t current_file_size = std::filesystem::file_size(path, file_size_error);

    if (file_size_error) {
        throw std::system_error(file_size_error,
                                "failed to inspect WAL before repair: " + path.string());
    }

    if (current_file_size != static_cast<std::uintmax_t>(expected_file_size)) {
        throw std::runtime_error("WAL changed after recovery: " + path.string());
    }

    std::error_code resize_error;

    std::filesystem::resize_file(path, static_cast<std::uintmax_t>(replay_result.valid_bytes),
                                 resize_error);

    if (resize_error) {
        throw std::system_error(resize_error, "failed to truncate WAL: " + path.string());
    }

    WalWriter writer(path, WalDurability::buffered);

    if (writer.size_bytes() != replay_result.valid_bytes) {
        throw std::runtime_error("WAL size mismatch after repair: " + path.string());
    }

    writer.sync();
}

} // namespace chronostore::internal