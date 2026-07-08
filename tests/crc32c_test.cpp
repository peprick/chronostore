#include "internal/crc32c.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace chronostore::internal {
namespace {

TEST(Crc32cTest, EmptyInputHasStandardChecksum) {
    const std::vector<std::uint8_t> bytes;

    EXPECT_EQ(crc32c(bytes), 0x00000000U);
}

TEST(Crc32cTest, MatchesStandardCheckValue) {
    const std::vector<std::uint8_t> bytes{0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
                                          0x36U, 0x37U, 0x38U, 0x39U};

    EXPECT_EQ(crc32c(bytes), 0xE3069283U);
}

TEST(Crc32cTest, ChangesWhenInputChanges) {
    const std::vector<std::uint8_t> original{0x10U, 0x20U, 0x30U, 0x40U};

    const std::vector<std::uint8_t> corrupted{0x10U, 0x20U, 0x31U, 0x40U};

    EXPECT_NE(crc32c(original), crc32c(corrupted));
}

} // namespace
} // namespace chronostore::internal
