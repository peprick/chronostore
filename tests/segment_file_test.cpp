#include "internal/segment_file.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace chronostore::internal {
namespace {

class SegmentFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        const ::testing::TestInfo* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        directory_ = std::filesystem::path(::testing::TempDir()) /
                     ("chronostore-segment-" + std::string(test_info->name()));
        path_ = directory_ / "segment-1.cst";

        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

SeriesKey alpha_series() {
    return SeriesKey{"alpha", {Tag{"host", "a"}}};
}

SeriesKey beta_series() {
    return SeriesKey{"beta", {Tag{"host", "b"}}};
}

std::vector<SegmentBlock> test_blocks() {
    return {
        SegmentBlock{alpha_series(), {Sample{Timestamp{0}, 10.0}, Sample{Timestamp{10}, 11.0}}},
        SegmentBlock{alpha_series(), {Sample{Timestamp{20}, 12.0}, Sample{Timestamp{30}, 13.0}}},
        SegmentBlock{beta_series(), {Sample{Timestamp{5}, 20.0}}}};
}

void flip_file_byte(const std::filesystem::path& path, std::uint64_t offset) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open segment test file");
    }

    file.seekg(static_cast<std::streamoff>(offset));
    char character = 0;
    file.get(character);

    if (!file) {
        throw std::runtime_error("failed to read segment test byte");
    }

    const std::uint8_t byte = std::bit_cast<std::uint8_t>(character);
    const std::uint8_t changed = static_cast<std::uint8_t>(byte ^ 0x01U);
    file.seekp(static_cast<std::streamoff>(offset));
    file.put(std::bit_cast<char>(changed));
    file.close();

    if (!file) {
        throw std::runtime_error("failed to modify segment test byte");
    }
}

TEST_F(SegmentFileTest, WritesAndOpensIndexedSegment) {
    const SegmentFileMetadata metadata = write_segment_file(path_, test_blocks());

    EXPECT_TRUE(std::filesystem::is_regular_file(path_));
    EXPECT_FALSE(std::filesystem::exists(path_.string() + ".tmp"));
    EXPECT_EQ(metadata.block_count, 3U);
    EXPECT_EQ(metadata.sample_count, 5U);
    EXPECT_EQ(metadata.file_size, std::filesystem::file_size(path_));

    const SegmentReader reader(path_);
    EXPECT_EQ(reader.path(), path_);
    EXPECT_EQ(reader.block_count(), 3U);
    EXPECT_EQ(reader.sample_count(), 5U);
    EXPECT_EQ(reader.file_size(), metadata.file_size);
    ASSERT_EQ(reader.index().size(), 3U);
    EXPECT_EQ(reader.index()[0].series, alpha_series());
    EXPECT_EQ(reader.index()[2].series, beta_series());
}

TEST_F(SegmentFileTest, QueriesLatestAndHalfOpenRangesAcrossBlocks) {
    static_cast<void>(write_segment_file(path_, test_blocks()));
    const SegmentReader reader(path_);

    const std::optional<Sample> latest = reader.latest(alpha_series());
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest.value(), (Sample{Timestamp{30}, 13.0}));
    EXPECT_FALSE(reader.latest(SeriesKey{"missing"}).has_value());

    const std::vector<Sample> samples = reader.range(alpha_series(), Timestamp{5}, Timestamp{25});
    const std::vector<Sample> expected{Sample{Timestamp{10}, 11.0}, Sample{Timestamp{20}, 12.0}};
    EXPECT_EQ(samples, expected);
    EXPECT_TRUE(reader.range(alpha_series(), Timestamp{10}, Timestamp{10}).empty());
    EXPECT_THROW(static_cast<void>(reader.range(alpha_series(), Timestamp{20}, Timestamp{10})),
                 std::invalid_argument);
}

TEST_F(SegmentFileTest, RejectsEmptyAndUnorderedBlocks) {
    EXPECT_THROW(static_cast<void>(write_segment_file(path_, {})), std::invalid_argument);

    std::vector<SegmentBlock> unordered{SegmentBlock{beta_series(), {Sample{Timestamp{0}, 1.0}}},
                                        SegmentBlock{alpha_series(), {Sample{Timestamp{0}, 2.0}}}};
    EXPECT_THROW(static_cast<void>(write_segment_file(path_, unordered)), std::invalid_argument);

    std::vector<SegmentBlock> overlapping{
        SegmentBlock{alpha_series(), {Sample{Timestamp{0}, 1.0}, Sample{Timestamp{10}, 2.0}}},
        SegmentBlock{alpha_series(), {Sample{Timestamp{10}, 3.0}}}};
    EXPECT_THROW(static_cast<void>(write_segment_file(path_, overlapping)), std::invalid_argument);
}

TEST_F(SegmentFileTest, AtomicallyReplacesExistingSegment) {
    static_cast<void>(write_segment_file(path_, test_blocks()));
    const std::vector<SegmentBlock> replacement{
        SegmentBlock{beta_series(), {Sample{Timestamp{100}, 42.0}}}};
    static_cast<void>(write_segment_file(path_, replacement));

    const SegmentReader reader(path_);
    EXPECT_EQ(reader.block_count(), 1U);
    EXPECT_EQ(reader.sample_count(), 1U);
    EXPECT_FALSE(reader.latest(alpha_series()).has_value());
    EXPECT_EQ(reader.latest(beta_series()).value(), (Sample{Timestamp{100}, 42.0}));
}

TEST_F(SegmentFileTest, RejectsCorruptedHeaderAndIndexAtOpen) {
    static_cast<void>(write_segment_file(path_, test_blocks()));
    flip_file_byte(path_, 0U);
    EXPECT_THROW(SegmentReader{path_}, SegmentFormatError);

    static_cast<void>(write_segment_file(path_, test_blocks()));
    const SegmentReader intact(path_);
    const SegmentIndexEntry& last = intact.index().back();
    const std::uint64_t index_offset = last.block_offset + last.block_size;
    flip_file_byte(path_, index_offset);
    EXPECT_THROW(SegmentReader{path_}, SegmentFormatError);
}

TEST_F(SegmentFileTest, DetectsCorruptedBlockWhenQueried) {
    static_cast<void>(write_segment_file(path_, test_blocks()));
    const SegmentReader intact(path_);
    const SegmentIndexEntry first_entry = intact.index().front();
    flip_file_byte(path_, first_entry.block_offset + 24U);

    const SegmentReader reader(path_);
    EXPECT_THROW(static_cast<void>(reader.range(alpha_series(), Timestamp{0}, Timestamp{1})),
                 SegmentFormatError);
}

TEST_F(SegmentFileTest, MissingFileReportsAnIoFailure) {
    EXPECT_THROW(SegmentReader{path_}, std::exception);
}

} // namespace
} // namespace chronostore::internal
