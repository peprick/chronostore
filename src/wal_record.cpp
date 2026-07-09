#include "internal/wal_record.hpp"

#include "internal/byte_codec.hpp"
#include "internal/crc32c.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronostore::internal {
namespace {

// Little-endian bytes spell "CSWL".
constexpr std::uint32_t wal_magic = 0x4C575343U;
constexpr std::uint8_t wal_version = 1U;
constexpr std::size_t max_payload_size = 16U * 1024U * 1024U;
constexpr std::size_t wal_header_size = 14;
constexpr std::size_t wal_checksum_size = 4;

void write_string(ByteWriter& writer, const std::string& value) {
    const std::size_t maximum_length = std::numeric_limits<std::uint32_t>::max();

    if (value.size() > maximum_length) {
        throw std::length_error("WAL string is too large");
    }

    writer.write_u32(static_cast<std::uint32_t>(value.size()));

    for (char character : value) {
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(character));

        writer.write_u8(byte);
    }
}

std::vector<std::uint8_t> encode_payload(const WalPutRecord& record) {
    ByteWriter writer;

    write_string(writer, record.series.measurement());

    const std::vector<Tag>& tags = record.series.tags();

    if (tags.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("too many WAL tags");
    }

    writer.write_u32(static_cast<std::uint32_t>(tags.size()));

    for (const Tag& tag : tags) {
        write_string(writer, tag.key());
        write_string(writer, tag.value());
    }

    const std::int64_t timestamp = record.sample.timestamp().nanoseconds_since_epoch();

    const std::uint64_t timestamp_bits = std::bit_cast<std::uint64_t>(timestamp);

    const std::uint64_t value_bits = std::bit_cast<std::uint64_t>(record.sample.value());

    writer.write_u64(timestamp_bits);
    writer.write_u64(value_bits);

    return writer.data();
}

WalDecodeResult make_decode_error(WalDecodeError error) {
    WalDecodeResult result;
    result.error = error;
    return result;
}

bool read_string(ByteReader& reader, std::string& value) {
    std::uint32_t encoded_length = 0;

    if (!reader.read_u32(encoded_length)) {
        return false;
    }

    const std::size_t length = encoded_length;

    if (length > reader.remaining()) {
        return false;
    }

    std::string decoded;
    decoded.reserve(length);

    for (std::size_t index = 0; index < length; ++index) {
        std::uint8_t byte = 0;

        if (!reader.read_u8(byte)) {
            return false;
        }

        decoded.push_back(std::bit_cast<char>(byte));
    }

    value = std::move(decoded);
    return true;
}

std::optional<WalPutRecord> decode_payload(std::span<const std::uint8_t> payload) {
    ByteReader reader(payload);

    try {
        std::string measurement;

        if (!read_string(reader, measurement)) {
            return std::nullopt;
        }

        std::uint32_t tag_count = 0;

        if (!reader.read_u32(tag_count)) {
            return std::nullopt;
        }

        std::vector<Tag> tags;

        for (std::uint32_t index = 0; index < tag_count; ++index) {
            std::string key;
            std::string value;

            if (!read_string(reader, key)) {
                return std::nullopt;
            }

            if (!read_string(reader, value)) {
                return std::nullopt;
            }

            tags.emplace_back(std::move(key), std::move(value));
        }

        std::uint64_t timestamp_bits = 0;
        std::uint64_t value_bits = 0;

        if (!reader.read_u64(timestamp_bits)) {
            return std::nullopt;
        }

        if (!reader.read_u64(value_bits)) {
            return std::nullopt;
        }

        if (reader.remaining() != 0) {
            return std::nullopt;
        }

        const std::int64_t timestamp = std::bit_cast<std::int64_t>(timestamp_bits);

        const double value = std::bit_cast<double>(value_bits);

        return WalPutRecord{SeriesKey{std::move(measurement), std::move(tags)},
                            Sample{Timestamp{timestamp}, value}};
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}
} // namespace

std::vector<std::uint8_t> encode_wal_record(const WalPutRecord& record) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);

    const std::vector<std::uint8_t> payload = encode_payload(record);

    if (payload.size() > max_payload_size) {
        throw std::length_error("WAL payload is too large");
    }

    const auto payload_length = static_cast<std::uint32_t>(payload.size());

    const std::uint32_t inverted_length =
        payload_length ^ std::numeric_limits<std::uint32_t>::max();

    ByteWriter writer;

    writer.write_u32(wal_magic);
    writer.write_u8(wal_version);
    writer.write_u8(static_cast<std::uint8_t>(WalRecordType::put));
    writer.write_u32(payload_length);
    writer.write_u32(inverted_length);
    writer.write_bytes(payload);

    const std::uint32_t checksum = crc32c(writer.data());
    writer.write_u32(checksum);

    return writer.data();
}

WalDecodeResult decode_wal_record(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < wal_header_size) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    ByteReader reader(bytes);

    std::uint32_t magic = 0;
    std::uint8_t version = 0;
    std::uint8_t record_type = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t inverted_length = 0;

    if (!reader.read_u32(magic)) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    if (!reader.read_u8(version)) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    if (!reader.read_u8(record_type)) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    if (!reader.read_u32(payload_length)) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    if (!reader.read_u32(inverted_length)) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    if (magic != wal_magic) {
        return make_decode_error(WalDecodeError::invalid_magic);
    }

    const std::uint32_t expected_inverted_length =
        payload_length ^ std::numeric_limits<std::uint32_t>::max();

    if (inverted_length != expected_inverted_length) {
        return make_decode_error(WalDecodeError::invalid_length);
    }

    const std::size_t payload_size = payload_length;

    if (payload_size > max_payload_size) {
        return make_decode_error(WalDecodeError::invalid_length);
    }

    const std::size_t protected_size = wal_header_size + payload_size;

    const std::size_t total_record_size = protected_size + wal_checksum_size;

    if (bytes.size() < total_record_size) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    const std::span<const std::uint8_t> protected_bytes = bytes.first(protected_size);

    const std::span<const std::uint8_t> checksum_bytes =
        bytes.subspan(protected_size, wal_checksum_size);

    ByteReader checksum_reader(checksum_bytes);

    std::uint32_t stored_checksum = 0;

    if (!checksum_reader.read_u32(stored_checksum)) {
        return make_decode_error(WalDecodeError::incomplete_record);
    }

    const std::uint32_t calculated_checksum = crc32c(protected_bytes);

    if (stored_checksum != calculated_checksum) {
        return make_decode_error(WalDecodeError::checksum_mismatch);
    }

    if (version != wal_version) {
        return make_decode_error(WalDecodeError::unsupported_version);
    }

    const auto expected_record_type = static_cast<std::uint8_t>(WalRecordType::put);

    if (record_type != expected_record_type) {
        return make_decode_error(WalDecodeError::unsupported_record_type);
    }

    const std::span<const std::uint8_t> payload = bytes.subspan(wal_header_size, payload_size);

    std::optional<WalPutRecord> decoded_record = decode_payload(payload);

    if (!decoded_record.has_value()) {
        return make_decode_error(WalDecodeError::invalid_payload);
    }

    WalDecodeResult result;
    result.record = std::move(decoded_record);
    result.bytes_consumed = total_record_size;

    return result;
}

} // namespace chronostore::internal
