#include "internal/crc32c.hpp"
#include "internal/wal_record.hpp"
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronostore::internal {
namespace {

WalPutRecord make_test_record() {
    return WalPutRecord{SeriesKey{"temperature", {Tag{"unit", "celsius"}, Tag{"room", "lab"}}},
                        Sample{Timestamp{-123456789}, -17.25}};
}

void write_u32_at(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    constexpr std::size_t encoded_size = 4;

    for (std::size_t index = 0; index < encoded_size; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value & 0xFFU);

        value >>= 8U;
    }
}

void refresh_checksum(std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t checksum_size = 4;

    const std::size_t checksum_offset = bytes.size() - checksum_size;

    const std::span<const std::uint8_t> protected_bytes{bytes.data(), checksum_offset};

    const std::uint32_t checksum = crc32c(protected_bytes);

    write_u32_at(bytes, checksum_offset, checksum);
}

TEST(WalRecordEncoderTest, EncodesStableVersionOneLayout) {
    const WalPutRecord record{SeriesKey{"cpu", {Tag{"host", "alpha"}}},
                              Sample{Timestamp{-42}, 12.5}};

    const std::vector<std::uint8_t> encoded = encode_wal_record(record);

    const std::vector<std::uint8_t> expected{
        0x43U, 0x53U, 0x57U, 0x4CU, // "CSWL"
        0x01U,                      // Version
        0x01U,                      // PUT record type
        0x2CU, 0x00U, 0x00U, 0x00U, // Payload length: 44
        0xD3U, 0xFFU, 0xFFU, 0xFFU, // Inverted length

        0x03U, 0x00U, 0x00U, 0x00U, // Measurement length
        0x63U, 0x70U, 0x75U,        // "cpu"
        0x01U, 0x00U, 0x00U, 0x00U, // Tag count

        0x04U, 0x00U, 0x00U, 0x00U,        // Tag-key length
        0x68U, 0x6FU, 0x73U, 0x74U,        // "host"
        0x05U, 0x00U, 0x00U, 0x00U,        // Tag-value length
        0x61U, 0x6CU, 0x70U, 0x68U, 0x61U, // "alpha"

        0xD6U, 0xFFU, 0xFFU, 0xFFU, // Timestamp: -42
        0xFFU, 0xFFU, 0xFFU, 0xFFU,

        0x00U, 0x00U, 0x00U, 0x00U, // IEEE 754 value: 12.5
        0x00U, 0x00U, 0x29U, 0x40U,

        0x14U, 0xDCU, 0x0EU, 0x53U // CRC32C
    };

    EXPECT_EQ(encoded, expected);
}

TEST(WalRecordDecoderTest, RoundTripPreservesRecord) {
    const WalPutRecord original = make_test_record();

    const std::vector<std::uint8_t> encoded = encode_wal_record(original);

    const WalDecodeResult result = decode_wal_record(encoded);

    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.record.has_value());

    EXPECT_EQ(result.error, WalDecodeError::none);
    EXPECT_EQ(result.record.value(), original);
    EXPECT_EQ(result.bytes_consumed, encoded.size());
}

TEST(WalRecordDecoderTest, EveryTruncatedPrefixIsIncomplete) {
    const std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    for (std::size_t prefix_size = 0; prefix_size < encoded.size(); ++prefix_size) {
        const std::span<const std::uint8_t> prefix{encoded.data(), prefix_size};

        const WalDecodeResult result = decode_wal_record(prefix);

        EXPECT_FALSE(result.success()) << "prefix size: " << prefix_size;

        EXPECT_EQ(result.error, WalDecodeError::incomplete_record)
            << "prefix size: " << prefix_size;

        EXPECT_EQ(result.bytes_consumed, 0U);
    }
}

TEST(WalRecordDecoderTest, DetectsPayloadCorruption) {
    std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    constexpr std::size_t payload_byte_offset = 20;

    encoded[payload_byte_offset] = static_cast<std::uint8_t>(encoded[payload_byte_offset] ^ 0x01U);

    const WalDecodeResult result = decode_wal_record(encoded);

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, WalDecodeError::checksum_mismatch);
    EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(WalRecordDecoderTest, DecodesConsecutiveRecords) {
    const WalPutRecord first = make_test_record();

    const WalPutRecord second{SeriesKey{"pressure", {Tag{"sensor", "p1"}}},
                              Sample{Timestamp{42}, 101.5}};

    const std::vector<std::uint8_t> first_bytes = encode_wal_record(first);

    const std::vector<std::uint8_t> second_bytes = encode_wal_record(second);

    std::vector<std::uint8_t> stream = first_bytes;

    for (std::uint8_t byte : second_bytes) {
        stream.push_back(byte);
    }

    const std::span<const std::uint8_t> stream_view{stream};

    const WalDecodeResult first_result = decode_wal_record(stream_view);

    ASSERT_TRUE(first_result.success());
    ASSERT_TRUE(first_result.record.has_value());
    EXPECT_EQ(first_result.record.value(), first);
    EXPECT_EQ(first_result.bytes_consumed, first_bytes.size());

    const std::span<const std::uint8_t> remaining =
        stream_view.subspan(first_result.bytes_consumed);

    const WalDecodeResult second_result = decode_wal_record(remaining);

    ASSERT_TRUE(second_result.success());
    ASSERT_TRUE(second_result.record.has_value());
    EXPECT_EQ(second_result.record.value(), second);
    EXPECT_EQ(second_result.bytes_consumed, second_bytes.size());
}

TEST(WalRecordDecoderTest, RejectsInvalidMagic) {
    std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    encoded[0] = 0x00U;

    const WalDecodeResult result = decode_wal_record(encoded);

    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.record.has_value());
    EXPECT_EQ(result.error, WalDecodeError::invalid_magic);
    EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(WalRecordDecoderTest, RejectsInconsistentLength) {
    std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    constexpr std::size_t inverted_length_offset = 10;

    encoded[inverted_length_offset] =
        static_cast<std::uint8_t>(encoded[inverted_length_offset] ^ 0x01U);

    const WalDecodeResult result = decode_wal_record(encoded);

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, WalDecodeError::invalid_length);
    EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(WalRecordDecoderTest, RejectsUnsupportedVersion) {
    std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    constexpr std::size_t version_offset = 4;
    encoded[version_offset] = 2U;

    refresh_checksum(encoded);

    const WalDecodeResult result = decode_wal_record(encoded);

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, WalDecodeError::unsupported_version);
}

TEST(WalRecordDecoderTest, RejectsUnsupportedRecordType) {
    std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    constexpr std::size_t record_type_offset = 5;
    encoded[record_type_offset] = 2U;

    refresh_checksum(encoded);

    const WalDecodeResult result = decode_wal_record(encoded);

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, WalDecodeError::unsupported_record_type);
}

TEST(WalRecordDecoderTest, RejectsNonFiniteSampleValue) {
    std::vector<std::uint8_t> encoded = encode_wal_record(make_test_record());

    constexpr std::size_t checksum_size = 4;
    constexpr std::size_t encoded_double_size = 8;

    const std::size_t value_offset = encoded.size() - checksum_size - encoded_double_size;

    for (std::size_t index = 0; index < encoded_double_size; ++index) {
        encoded[value_offset + index] = 0x00U;
    }

    // Little-endian IEEE 754 quiet NaN: 0x7FF8000000000000.
    encoded[value_offset + 6] = 0xF8U;
    encoded[value_offset + 7] = 0x7FU;

    refresh_checksum(encoded);

    const WalDecodeResult result = decode_wal_record(encoded);

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, WalDecodeError::invalid_payload);
}

} // namespace
} // namespace chronostore::internal
