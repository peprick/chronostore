#include "internal/wal_record.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace chronostore::internal {
namespace {

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

} // namespace
} // namespace chronostore::internal
