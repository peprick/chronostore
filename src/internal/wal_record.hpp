#ifndef CHRONOSTORE_INTERNAL_WAL_RECORD_HPP
#define CHRONOSTORE_INTERNAL_WAL_RECORD_HPP

#include <chronostore/model.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace chronostore::internal {

enum class WalRecordType : std::uint8_t { put = 1 };

struct WalPutRecord {
    SeriesKey series;
    Sample sample;

    bool operator==(const WalPutRecord&) const = default;
};

enum class WalDecodeError {
    none,
    incomplete_record,
    invalid_magic,
    unsupported_version,
    unsupported_record_type,
    invalid_length,
    checksum_mismatch,
    invalid_payload
};

struct WalDecodeResult {
    std::optional<WalPutRecord> record;
    WalDecodeError error{WalDecodeError::none};
    std::size_t bytes_consumed{0};

    [[nodiscard]] bool success() const noexcept {
        return record.has_value() && error == WalDecodeError::none;
    }
};

[[nodiscard]] std::vector<std::uint8_t> encode_wal_record(const WalPutRecord& record);

[[nodiscard]] WalDecodeResult decode_wal_record(std::span<const std::uint8_t> bytes);

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_WAL_RECORD_HPP
