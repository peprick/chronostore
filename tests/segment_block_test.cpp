#include "internal/segment_block.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace chronostore::internal {
namespace {

SeriesKey segment_series() {
    return SeriesKey{"temperature", {Tag{"room", "lab"}}};
}

std::vector<Sample> make_samples(std::size_t count) {
    std::vector<Sample> samples;
    samples.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        const auto timestamp = static_cast<std::int64_t>(index);

        const double value = static_cast<double>(index);

        samples.emplace_back(Timestamp{timestamp}, value);
    }

    return samples;
}

TEST(SegmentBlockTest, StoresSeriesSamplesAndTimestampBounds) {
    const SeriesKey series = segment_series();

    const std::vector<Sample> samples{Sample{Timestamp{100}, 20.0}, Sample{Timestamp{200}, 21.0},
                                      Sample{Timestamp{300}, 22.0}};

    const SegmentBlock block{series, samples};

    EXPECT_EQ(block.series(), series);
    EXPECT_EQ(block.samples(), samples);
    EXPECT_EQ(block.sample_count(), 3U);
    EXPECT_EQ(block.first_timestamp(), Timestamp{100});
    EXPECT_EQ(block.last_timestamp(), Timestamp{300});
}

TEST(SegmentBlockTest, AcceptsMaximumSampleCount) {
    const std::vector<Sample> samples = make_samples(max_samples_per_segment_block);

    const SegmentBlock block{segment_series(), samples};

    EXPECT_EQ(block.sample_count(), max_samples_per_segment_block);
}

TEST(SegmentBlockTest, RejectsEmptySampleCollection) {
    const std::vector<Sample> samples;

    EXPECT_THROW(static_cast<void>(SegmentBlock(segment_series(), samples)), std::invalid_argument);
}

TEST(SegmentBlockTest, RejectsTooManySamples) {
    const std::vector<Sample> samples = make_samples(max_samples_per_segment_block + 1U);

    EXPECT_THROW(static_cast<void>(SegmentBlock(segment_series(), samples)), std::length_error);
}

TEST(SegmentBlockTest, RejectsDuplicateTimestamps) {
    const std::vector<Sample> samples{Sample{Timestamp{100}, 20.0}, Sample{Timestamp{100}, 21.0}};

    EXPECT_THROW(static_cast<void>(SegmentBlock(segment_series(), samples)), std::invalid_argument);
}

TEST(SegmentBlockTest, RejectsOutOfOrderTimestamps) {
    const std::vector<Sample> samples{Sample{Timestamp{100}, 20.0}, Sample{Timestamp{300}, 22.0},
                                      Sample{Timestamp{200}, 21.0}};

    EXPECT_THROW(static_cast<void>(SegmentBlock(segment_series(), samples)), std::invalid_argument);
}

} // namespace
} // namespace chronostore::internal