#include <gtest/gtest.h>
#include <internal/memtable.hpp>
#include <stdexcept>

using chronostore::Sample;
using chronostore::SeriesKey;
using chronostore::Tag;
using chronostore::Timestamp;
using chronostore::internal::MemTable;
using chronostore::internal::MemTableSeriesSnapshot;

TEST(MemTableTest, StartsEmpty) {
    const MemTable table;

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.sample_count(), 0U);
}

TEST(MemTableTest, CountsNewSeriesAndTimestamps) {
    MemTable table;

    const SeriesKey cpu("cpu_usage");
    const SeriesKey memory("memory_usage");

    table.put(cpu, Sample(Timestamp(100), 10.0));
    table.put(cpu, Sample(Timestamp(200), 20.0));
    table.put(memory, Sample(Timestamp(100), 30.0));

    EXPECT_FALSE(table.empty());
    EXPECT_EQ(table.sample_count(), 3U);
}

TEST(MemTableTest, OverwriteReplacesValueWithoutIncreasingCount) {
    MemTable table;
    const SeriesKey series("cpu_usage");

    table.put(series, Sample(Timestamp(100), 10.0));
    table.put(series, Sample(Timestamp(100), 99.0));

    EXPECT_EQ(table.sample_count(), 1U);

    const auto latest = table.latest(series);

    ASSERT_TRUE(latest.has_value());

    const Sample& latest_sample = latest.value();

    EXPECT_EQ(latest_sample.timestamp(), Timestamp(100));
    EXPECT_DOUBLE_EQ(latest_sample.value(), 99.0);
}

TEST(MemTableTest, MissingSeriesHasNoLatestSample) {
    const MemTable table;
    const SeriesKey missing("missing_series");

    const auto latest = table.latest(missing);

    EXPECT_FALSE(latest.has_value());
}

TEST(MemTableTest, LatestUsesGreatestTimestamp) {
    MemTable table;
    const SeriesKey series("cpu_usage");

    table.put(series, Sample(Timestamp(300), 30.0));
    table.put(series, Sample(Timestamp(100), 10.0));
    table.put(series, Sample(Timestamp(200), 20.0));

    const auto latest = table.latest(series);

    ASSERT_TRUE(latest.has_value());

    const Sample& latest_sample = latest.value();

    EXPECT_EQ(latest_sample.timestamp(), Timestamp(300));
    EXPECT_DOUBLE_EQ(latest_sample.value(), 30.0);
}

TEST(MemTableTest, MissingSeriesReturnsEmptyRange) {
    const MemTable table;
    const SeriesKey missing("missing_series");

    const auto result = table.range(missing, Timestamp(100), Timestamp(200));

    EXPECT_TRUE(result.empty());
}

TEST(MemTableTest, RangeIsOrderedAndHalfOpen) {
    MemTable table;
    const SeriesKey series("cpu_usage");

    table.put(series, Sample(Timestamp(400), 40.0));
    table.put(series, Sample(Timestamp(100), 10.0));
    table.put(series, Sample(Timestamp(300), 30.0));
    table.put(series, Sample(Timestamp(200), 20.0));

    const auto result = table.range(series, Timestamp(200), Timestamp(400));

    ASSERT_EQ(result.size(), 2U);

    EXPECT_EQ(result[0].timestamp(), Timestamp(200));
    EXPECT_DOUBLE_EQ(result[0].value(), 20.0);

    EXPECT_EQ(result[1].timestamp(), Timestamp(300));
    EXPECT_DOUBLE_EQ(result[1].value(), 30.0);
}

TEST(MemTableTest, EqualRangeBoundsReturnEmpty) {
    MemTable table;
    const SeriesKey series("cpu_usage");

    table.put(series, Sample(Timestamp(200), 20.0));

    const auto result = table.range(series, Timestamp(200), Timestamp(200));

    EXPECT_TRUE(result.empty());
}

TEST(MemTableTest, ReversedRangeIsRejected) {
    const MemTable table;
    const SeriesKey series("cpu_usage");

    EXPECT_THROW(static_cast<void>(table.range(series, Timestamp(300), Timestamp(200))),
                 std::invalid_argument);
}

TEST(MemTableTest, RangeDoesNotMixDifferentSeries) {
    MemTable table;

    const SeriesKey server_one("cpu_usage", {Tag("host", "server-01")});

    const SeriesKey server_two("cpu_usage", {Tag("host", "server-02")});

    table.put(server_one, Sample(Timestamp(100), 10.0));

    table.put(server_two, Sample(Timestamp(100), 90.0));

    const auto result = table.range(server_one, Timestamp(0), Timestamp(200));

    ASSERT_EQ(result.size(), 1U);
    EXPECT_DOUBLE_EQ(result[0].value(), 10.0);
}

TEST(MemTableTest, RangeReturnsOverwrittenValue) {
    MemTable table;
    const SeriesKey series("cpu_usage");

    table.put(series, Sample(Timestamp(100), 10.0));
    table.put(series, Sample(Timestamp(100), 99.0));

    const auto result = table.range(series, Timestamp(0), Timestamp(200));

    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].timestamp(), Timestamp(100));
    EXPECT_DOUBLE_EQ(result[0].value(), 99.0);
    EXPECT_EQ(table.sample_count(), 1U);
}

TEST(MemTableTest, SnapshotIsOrderedAndClearResetsState) {
    MemTable table;
    const SeriesKey beta{"beta"};
    const SeriesKey alpha{"alpha"};
    table.put(beta, Sample{Timestamp{20}, 2.0});
    table.put(alpha, Sample{Timestamp{10}, 1.0});
    table.put(alpha, Sample{Timestamp{30}, 3.0});

    const std::vector<MemTableSeriesSnapshot> snapshot = table.snapshot();
    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].series, alpha);
    EXPECT_EQ(snapshot[0].samples,
              (std::vector<Sample>{Sample{Timestamp{10}, 1.0}, Sample{Timestamp{30}, 3.0}}));
    EXPECT_EQ(snapshot[1].series, beta);

    table.clear();
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.sample_count(), 0U);
    EXPECT_TRUE(table.snapshot().empty());
}
