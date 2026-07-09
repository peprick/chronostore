#include "internal/segment_block_codec.hpp"

#include "internal/byte_codec.hpp"
#include "internal/crc32c.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronostore::internal {
namespace {

// Little-endian bytes spell "CSBK".
constexpr std::uint32_t segment_block_magic = 0x4B425343U;

constexpr std::uint16_t segment_block_version = 1U;
constexpr std::uint16_t segment_block_flags = 0U;

constexpr std::size_t max_segment_block_payload_size = 16U * 1024U * 1024U;
constexpr std::size_t segment_block_header_size = 20U;
constexpr std::size_t segment_block_checksum_size = sizeof(std::uint32_t);
constexpr std::size_t encoded_sample_size = 2U * sizeof(std::uint64_t);
constexpr std::size_t minimum_encoded_tag_size = 2U * sizeof(std::uint32_t);

void write_string(ByteWriter& writer, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("segment block string is too large");
    }

    writer.write_u32(static_cast<std::uint32_t>(value.size()));

    for (char character : value) {
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(character));

        writer.write_u8(byte);
    }
}

std::vector<std::uint8_t> encode_payload(const SegmentBlock& block) {
    ByteWriter writer;

    write_string(writer, block.series().measurement());

    const std::vector<Tag>& tags = block.series().tags();

    if (tags.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("segment block contains too many tags");
    }

    writer.write_u32(static_cast<std::uint32_t>(tags.size()));

    for (const Tag& tag : tags) {
        write_string(writer, tag.key());
        write_string(writer, tag.value());
    }

    for (const Sample& sample : block.samples()) {
        const std::int64_t timestamp = sample.timestamp().nanoseconds_since_epoch();

        const std::uint64_t timestamp_bits = std::bit_cast<std::uint64_t>(timestamp);

        const std::uint64_t value_bits = std::bit_cast<std::uint64_t>(sample.value());

        writer.write_u64(timestamp_bits);
        writer.write_u64(value_bits);
    }

    return writer.data();
}

SegmentBlockDecodeResult make_decode_error(SegmentBlockDecodeError error) {
    SegmentBlockDecodeResult result;
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

std::optional<SegmentBlock> decode_payload(std::span<const std::uint8_t> payload,
                                           std::uint32_t sample_count) {
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

        const std::size_t sample_bytes =
            static_cast<std::size_t>(sample_count) * encoded_sample_size;

        if (sample_bytes > reader.remaining()) {
            return std::nullopt;
        }

        const std::size_t bytes_available_for_tags = reader.remaining() - sample_bytes;

        if (static_cast<std::size_t>(tag_count) >
            bytes_available_for_tags / minimum_encoded_tag_size) {
            return std::nullopt;
        }

        std::vector<Tag> tags;

        for (std::uint32_t index = 0; index < tag_count; ++index) {
            std::string key;
            std::string value;

            if (!read_string(reader, key) || !read_string(reader, value)) {
                return std::nullopt;
            }

            if (!tags.empty() && key <= tags.back().key()) {
                return std::nullopt;
            }

            tags.emplace_back(std::move(key), std::move(value));
        }

        std::vector<Sample> samples;
        samples.reserve(sample_count);

        for (std::uint32_t index = 0; index < sample_count; ++index) {
            std::uint64_t timestamp_bits = 0;
            std::uint64_t value_bits = 0;

            if (!reader.read_u64(timestamp_bits) || !reader.read_u64(value_bits)) {
                return std::nullopt;
            }

            const std::int64_t timestamp = std::bit_cast<std::int64_t>(timestamp_bits);
            const double value = std::bit_cast<double>(value_bits);

            samples.emplace_back(Timestamp{timestamp}, value);
        }

        if (reader.remaining() != 0U) {
            return std::nullopt;
        }

        return SegmentBlock{SeriesKey{std::move(measurement), std::move(tags)}, std::move(samples)};
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::length_error&) {
        return std::nullopt;
    }
}

} // namespace

std::vector<std::uint8_t> encode_segment_block(const SegmentBlock& block) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));

    static_assert(std::numeric_limits<double>::is_iec559);

    const std::vector<std::uint8_t> payload = encode_payload(block);

    if (payload.size() > max_segment_block_payload_size) {
        throw std::length_error("segment block payload is too large");
    }

    const auto payload_length = static_cast<std::uint32_t>(payload.size());

    const std::uint32_t inverted_length =
        payload_length ^ std::numeric_limits<std::uint32_t>::max();

    const auto sample_count = static_cast<std::uint32_t>(block.sample_count());

    ByteWriter writer;

    writer.write_u32(segment_block_magic);
    writer.write_u16(segment_block_version);
    writer.write_u16(segment_block_flags);
    writer.write_u32(payload_length);
    writer.write_u32(inverted_length);
    writer.write_u32(sample_count);
    writer.write_bytes(payload);

    const std::uint32_t checksum = crc32c(writer.data());

    writer.write_u32(checksum);

    return writer.data();
}

SegmentBlockDecodeResult decode_segment_block(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < segment_block_header_size) {
        return make_decode_error(SegmentBlockDecodeError::incomplete_block);
    }

    ByteReader reader(bytes);

    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t inverted_length = 0;
    std::uint32_t sample_count = 0;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(flags) ||
        !reader.read_u32(payload_length) || !reader.read_u32(inverted_length) ||
        !reader.read_u32(sample_count)) {
        return make_decode_error(SegmentBlockDecodeError::incomplete_block);
    }

    if (magic != segment_block_magic) {
        return make_decode_error(SegmentBlockDecodeError::invalid_magic);
    }

    const std::uint32_t expected_inverted_length =
        payload_length ^ std::numeric_limits<std::uint32_t>::max();

    if (inverted_length != expected_inverted_length ||
        payload_length > max_segment_block_payload_size) {
        return make_decode_error(SegmentBlockDecodeError::invalid_length);
    }

    const std::size_t payload_size = payload_length;
    const std::size_t protected_size = segment_block_header_size + payload_size;
    const std::size_t total_block_size = protected_size + segment_block_checksum_size;

    if (bytes.size() < total_block_size) {
        return make_decode_error(SegmentBlockDecodeError::incomplete_block);
    }

    const std::span<const std::uint8_t> protected_bytes = bytes.first(protected_size);
    const std::span<const std::uint8_t> checksum_bytes =
        bytes.subspan(protected_size, segment_block_checksum_size);

    ByteReader checksum_reader(checksum_bytes);
    std::uint32_t stored_checksum = 0;

    if (!checksum_reader.read_u32(stored_checksum)) {
        return make_decode_error(SegmentBlockDecodeError::incomplete_block);
    }

    if (stored_checksum != crc32c(protected_bytes)) {
        return make_decode_error(SegmentBlockDecodeError::checksum_mismatch);
    }

    if (version != segment_block_version) {
        return make_decode_error(SegmentBlockDecodeError::unsupported_version);
    }

    if (flags != segment_block_flags) {
        return make_decode_error(SegmentBlockDecodeError::unsupported_flags);
    }

    if (sample_count == 0U || sample_count > max_samples_per_segment_block) {
        return make_decode_error(SegmentBlockDecodeError::invalid_sample_count);
    }

    const std::span<const std::uint8_t> payload =
        bytes.subspan(segment_block_header_size, payload_size);

    std::optional<SegmentBlock> decoded_block = decode_payload(payload, sample_count);

    if (!decoded_block.has_value()) {
        return make_decode_error(SegmentBlockDecodeError::invalid_payload);
    }

    SegmentBlockDecodeResult result;
    result.block = std::move(decoded_block);
    result.bytes_consumed = total_block_size;
    return result;
}

} // namespace chronostore::internal
