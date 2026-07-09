#include "internal/segment_block_codec.hpp"

#include "internal/crc32c.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronostore::internal {
namespace {

SegmentBlock codec_test_block() {
    return SegmentBlock{SeriesKey{"m"}, {Sample{Timestamp{-2}, 1.5}}};
}

SegmentBlock rich_codec_test_block() {
    return SegmentBlock{
        SeriesKey{"temperature", {Tag{"host", "alpha"}, Tag{"region", "west"}}},
        {Sample{Timestamp{-10}, 18.25}, Sample{Timestamp{0}, 19.5}, Sample{Timestamp{25}, 21.75}}};
}

void write_u16_little_endian(std::vector<std::uint8_t>& bytes, std::size_t offset,
                             std::uint16_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        const std::size_t shift = index * 8U;
        bytes[offset + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void write_u32_little_endian(std::vector<std::uint8_t>& bytes, std::size_t offset,
                             std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        const std::size_t shift = index * 8U;
        bytes[offset + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void append_u32_little_endian(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        const std::size_t shift = index * 8U;

        const auto byte = static_cast<std::uint8_t>((value >> shift) & 0xFFU);

        bytes.push_back(byte);
    }
}

void refresh_checksum(std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t checksum_size = sizeof(std::uint32_t);
    const std::size_t protected_size = bytes.size() - checksum_size;
    const std::span<const std::uint8_t> protected_bytes{bytes.data(), protected_size};
    write_u32_little_endian(bytes, protected_size, crc32c(protected_bytes));
}

TEST(SegmentBlockEncoderTest, EncodesStableVersionOneLayout) {
    const SegmentBlock block = codec_test_block();

    const std::vector<std::uint8_t> encoded = encode_segment_block(block);

    std::vector<std::uint8_t> expected{// Magic: "CSBK"
                                       0x43, 0x53, 0x42, 0x4B,

                                       // Version 1 and flags 0
                                       0x01, 0x00, 0x00, 0x00,

                                       // Payload length: 25
                                       0x19, 0x00, 0x00, 0x00,

                                       // Inverted payload length
                                       0xE6, 0xFF, 0xFF, 0xFF,

                                       // Sample count: 1
                                       0x01, 0x00, 0x00, 0x00,

                                       // Measurement string: "m"
                                       0x01, 0x00, 0x00, 0x00, 0x6D,

                                       // Tag count: 0
                                       0x00, 0x00, 0x00, 0x00,

                                       // Timestamp: -2
                                       0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

                                       // Double value: 1.5
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F};

    const std::span<const std::uint8_t> protected_bytes{expected};

    const std::uint32_t checksum = crc32c(protected_bytes);

    append_u32_little_endian(expected, checksum);

    EXPECT_EQ(encoded, expected);
}

TEST(SegmentBlockEncoderTest, StoresChecksumOfHeaderAndPayload) {
    const std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());

    constexpr std::size_t checksum_size = sizeof(std::uint32_t);

    ASSERT_GT(encoded.size(), checksum_size);

    const std::size_t protected_size = encoded.size() - checksum_size;

    std::uint32_t stored_checksum = 0;

    for (std::size_t index = 0; index < checksum_size; ++index) {
        const std::size_t shift = index * 8U;

        const auto byte = static_cast<std::uint32_t>(encoded[protected_size + index]);

        stored_checksum |= byte << shift;
    }

    const std::span<const std::uint8_t> protected_bytes{encoded.data(), protected_size};

    const std::uint32_t calculated_checksum = crc32c(protected_bytes);

    EXPECT_EQ(stored_checksum, calculated_checksum);
}

TEST(SegmentBlockDecoderTest, RoundTripPreservesBlock) {
    const SegmentBlock original = rich_codec_test_block();
    const std::vector<std::uint8_t> encoded = encode_segment_block(original);

    const SegmentBlockDecodeResult result = decode_segment_block(encoded);

    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.block.has_value());
    EXPECT_EQ(result.block.value(), original);
    EXPECT_EQ(result.bytes_consumed, encoded.size());
}

TEST(SegmentBlockDecoderTest, EveryTruncatedPrefixIsIncomplete) {
    const std::vector<std::uint8_t> encoded = encode_segment_block(rich_codec_test_block());

    for (std::size_t prefix_size = 0; prefix_size < encoded.size(); ++prefix_size) {
        SCOPED_TRACE(prefix_size);
        const std::span<const std::uint8_t> prefix{encoded.data(), prefix_size};
        const SegmentBlockDecodeResult result = decode_segment_block(prefix);

        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.error, SegmentBlockDecodeError::incomplete_block);
        EXPECT_EQ(result.bytes_consumed, 0U);
    }
}

TEST(SegmentBlockDecoderTest, DetectsChecksumCorruption) {
    std::vector<std::uint8_t> encoded = encode_segment_block(rich_codec_test_block());
    constexpr std::size_t first_measurement_byte = 24U;
    encoded[first_measurement_byte] =
        static_cast<std::uint8_t>(encoded[first_measurement_byte] ^ 0x01U);

    const SegmentBlockDecodeResult result = decode_segment_block(encoded);

    EXPECT_EQ(result.error, SegmentBlockDecodeError::checksum_mismatch);
    EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(SegmentBlockDecoderTest, DecodesConsecutiveBlocks) {
    const SegmentBlock first = codec_test_block();
    const SegmentBlock second = rich_codec_test_block();
    const std::vector<std::uint8_t> first_bytes = encode_segment_block(first);
    const std::vector<std::uint8_t> second_bytes = encode_segment_block(second);

    std::vector<std::uint8_t> stream;
    stream.reserve(first_bytes.size() + second_bytes.size());
    stream.insert(stream.end(), first_bytes.begin(), first_bytes.end());
    stream.insert(stream.end(), second_bytes.begin(), second_bytes.end());

    const std::span<const std::uint8_t> bytes{stream};
    const SegmentBlockDecodeResult first_result = decode_segment_block(bytes);

    ASSERT_TRUE(first_result.success());
    EXPECT_EQ(first_result.block.value(), first);
    EXPECT_EQ(first_result.bytes_consumed, first_bytes.size());

    const SegmentBlockDecodeResult second_result =
        decode_segment_block(bytes.subspan(first_result.bytes_consumed));

    ASSERT_TRUE(second_result.success());
    EXPECT_EQ(second_result.block.value(), second);
    EXPECT_EQ(second_result.bytes_consumed, second_bytes.size());
}

TEST(SegmentBlockDecoderTest, RejectsInvalidMagic) {
    std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());
    encoded[0] = 0U;

    EXPECT_EQ(decode_segment_block(encoded).error, SegmentBlockDecodeError::invalid_magic);
}

TEST(SegmentBlockDecoderTest, RejectsInconsistentLength) {
    std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());
    constexpr std::size_t inverted_length_offset = 12U;
    encoded[inverted_length_offset] =
        static_cast<std::uint8_t>(encoded[inverted_length_offset] ^ 0x01U);

    EXPECT_EQ(decode_segment_block(encoded).error, SegmentBlockDecodeError::invalid_length);
}

TEST(SegmentBlockDecoderTest, RejectsUnsupportedVersion) {
    std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());
    write_u16_little_endian(encoded, 4U, 2U);
    refresh_checksum(encoded);

    EXPECT_EQ(decode_segment_block(encoded).error, SegmentBlockDecodeError::unsupported_version);
}

TEST(SegmentBlockDecoderTest, RejectsUnsupportedFlags) {
    std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());
    write_u16_little_endian(encoded, 6U, 1U);
    refresh_checksum(encoded);

    EXPECT_EQ(decode_segment_block(encoded).error, SegmentBlockDecodeError::unsupported_flags);
}

TEST(SegmentBlockDecoderTest, RejectsInvalidSampleCount) {
    std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());
    write_u32_little_endian(encoded, 16U, 0U);
    refresh_checksum(encoded);

    EXPECT_EQ(decode_segment_block(encoded).error, SegmentBlockDecodeError::invalid_sample_count);
}

TEST(SegmentBlockDecoderTest, RejectsInvalidPayload) {
    std::vector<std::uint8_t> encoded = encode_segment_block(codec_test_block());
    constexpr std::size_t measurement_length_offset = 20U;
    write_u32_little_endian(encoded, measurement_length_offset, 0U);
    refresh_checksum(encoded);

    EXPECT_EQ(decode_segment_block(encoded).error, SegmentBlockDecodeError::invalid_payload);
}

} // namespace
} // namespace chronostore::internal
