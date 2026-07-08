#include "internal/wal_record.hpp"

#include "internal/byte_codec.hpp"
#include "internal/crc32c.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronostore::internal {
namespace {

// Little-endian bytes spell "CSWL".
constexpr std::uint32_t wal_magic = 0x4C575343U;
constexpr std::uint8_t wal_version = 1U;
constexpr std::size_t max_payload_size = 16U * 1024U * 1024U;

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

} // namespace chronostore::internal
