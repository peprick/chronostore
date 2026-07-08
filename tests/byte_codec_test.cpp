#include "internal/byte_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace chronostore::internal {
namespace {

TEST(ByteWriterTest, WritesIntegersInLittleEndianOrder) {
    ByteWriter writer;

    writer.write_u8(0xABU);
    writer.write_u16(0x1234U);
    writer.write_u32(0x89ABCDEFU);
    writer.write_u64(0x0123456789ABCDEFULL);

    const std::vector<std::uint8_t> expected{0xABU, 0x34U, 0x12U, 0xEFU, 0xCDU, 0xABU, 0x89U, 0xEFU,
                                             0xCDU, 0xABU, 0x89U, 0x67U, 0x45U, 0x23U, 0x01U};

    EXPECT_EQ(writer.data(), expected);
}

TEST(ByteReaderTest, ReadsIntegersAndTracksPosition) {
    const std::vector<std::uint8_t> bytes{0xABU, 0x34U, 0x12U, 0xEFU, 0xCDU, 0xABU, 0x89U};

    ByteReader reader(bytes);

    std::uint8_t value8 = 0;
    std::uint16_t value16 = 0;
    std::uint32_t value32 = 0;

    EXPECT_TRUE(reader.read_u8(value8));
    EXPECT_EQ(value8, 0xABU);
    EXPECT_EQ(reader.position(), 1U);

    EXPECT_TRUE(reader.read_u16(value16));
    EXPECT_EQ(value16, 0x1234U);
    EXPECT_EQ(reader.position(), 3U);

    EXPECT_TRUE(reader.read_u32(value32));
    EXPECT_EQ(value32, 0x89ABCDEFU);
    EXPECT_EQ(reader.position(), 7U);
    EXPECT_EQ(reader.remaining(), 0U);
}

TEST(ByteReaderTest, FailedIntegerReadPreservesState) {
    const std::vector<std::uint8_t> bytes{0x34U};
    ByteReader reader(bytes);

    std::uint16_t value = 0xBEEFU;

    EXPECT_FALSE(reader.read_u16(value));
    EXPECT_EQ(value, 0xBEEFU);
    EXPECT_EQ(reader.position(), 0U);
    EXPECT_EQ(reader.remaining(), 1U);
}

TEST(ByteCodecTest, WritesAndReadsRawBytes) {
    const std::vector<std::uint8_t> payload{0x00U, 0x10U, 0xFEU, 0xFFU};

    ByteWriter writer;
    writer.write_bytes(payload);

    ByteReader reader(writer.data());
    std::vector<std::uint8_t> output;

    EXPECT_TRUE(reader.read_bytes(payload.size(), output));
    EXPECT_EQ(output, payload);
    EXPECT_EQ(reader.remaining(), 0U);

    const std::vector<std::uint8_t> sentinel{0xAAU};
    output = sentinel;

    EXPECT_FALSE(reader.read_bytes(1, output));
    EXPECT_EQ(output, sentinel);
    EXPECT_EQ(reader.position(), payload.size());
}

} // namespace
} // namespace chronostore::internal
