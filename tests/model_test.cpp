#include <chronostore/model.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

using chronostore::Sample;
using chronostore::SeriesKey;
using chronostore::Tag;
using chronostore::Timestamp;

static_assert(!std::is_default_constructible_v<Timestamp>);
static_assert(!std::is_convertible_v<std::int64_t, Timestamp>);

TEST(TagTest, StoresKeyAndValue) {
    const Tag tag("host", "server-01");

    EXPECT_EQ(tag.key(), "host");
    EXPECT_EQ(tag.value(), "server-01");
}

TEST(TagTest, RejectsEmptyKey) {
    EXPECT_THROW(static_cast<void>(Tag("", "production")), std::invalid_argument);
}

TEST(TagTest, ComparesByContent) {
    const Tag first("host", "server-01");
    const Tag same("host", "server-01");
    const Tag different("host", "server-02");

    EXPECT_EQ(first, same);
    EXPECT_NE(first, different);
}

TEST(TagTest, AllowsEmptyValue) {
    const Tag tag("environment", "");

    EXPECT_TRUE(tag.value().empty());
}

TEST(TagTest, OrdersByKeyThenValue) {
    const Tag hostA("host", "server-01");
    const Tag hostB("host", "server-02");
    const Tag region("region", "mumbai");

    EXPECT_LT(hostA, hostB);
    EXPECT_LT(hostB, region);
}

TEST(SeriesKeyTest, StoresMeasurementWithoutTags) {
    const SeriesKey series("cpu_usage");

    EXPECT_EQ(series.measurement(), "cpu_usage");
    EXPECT_TRUE(series.tags().empty());
}

TEST(SeriesKeyTest, RejectsEmptyMeasurement) {
    EXPECT_THROW(static_cast<void>(SeriesKey("")), std::invalid_argument);
}

TEST(SeriesKeyTest, CanonicalizesTagOrder) {
    const SeriesKey series("cpu_usage", {Tag("region", "mumbai"), Tag("host", "server-01")});

    ASSERT_EQ(series.tags().size(), 2U);
    EXPECT_EQ(series.tags()[0].key(), "host");
    EXPECT_EQ(series.tags()[1].key(), "region");
}

TEST(SeriesKeyTest, RejectsDuplicateTagKeys) {
    const std::vector<Tag> tags{Tag("host", "server-01"), Tag("host", "server-02")};

    EXPECT_THROW(static_cast<void>(SeriesKey("cpu_usage", tags)), std::invalid_argument);
}

TEST(SeriesKeyTest, InputTagOrderDoesNotAffectIdentity) {
    const SeriesKey first("cpu_usage", {Tag("host", "server-01"), Tag("region", "mumbai")});

    const SeriesKey reversed("cpu_usage", {Tag("region", "mumbai"), Tag("host", "server-01")});

    EXPECT_EQ(first, reversed);
}

TEST(SeriesKeyTest, DifferentMeasurementsAreNotEqual) {
    const SeriesKey cpu("cpu_usage", {Tag("host", "server-01")});

    const SeriesKey memory("memory_usage", {Tag("host", "server-01")});

    EXPECT_NE(cpu, memory);
}

TEST(SeriesKeyTest, DifferentTagValuesAreNotEqual) {
    const SeriesKey first("cpu_usage", {Tag("host", "server-01")});

    const SeriesKey second("cpu_usage", {Tag("host", "server-02")});

    EXPECT_NE(first, second);
}

TEST(TimestampTest, PreservesExactNanoseconds) {
    constexpr std::int64_t value = 1'783'308'123'456'789'000LL;

    constexpr Timestamp timestamp(value);

    static_assert(timestamp.nanoseconds_since_epoch() == value);
    EXPECT_EQ(timestamp.nanoseconds_since_epoch(), value);
}

TEST(TimestampTest, AcceptsTimesBeforeUnixEpoch) {
    constexpr Timestamp timestamp(-1);

    EXPECT_EQ(timestamp.nanoseconds_since_epoch(), -1);
}

TEST(TimestampTest, OrdersChronologically) {
    constexpr Timestamp before_epoch(-1);
    constexpr Timestamp epoch(0);
    constexpr Timestamp after_epoch(1);

    EXPECT_LT(before_epoch, epoch);
    EXPECT_LT(epoch, after_epoch);
}

TEST(SampleTest, StoresTimestampAndValue) {
    const Sample sample(Timestamp(123), 72.5);

    EXPECT_EQ(sample.timestamp(), Timestamp(123));
    EXPECT_DOUBLE_EQ(sample.value(), 72.5);
}

TEST(SampleTest, RejectsNaN) {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    EXPECT_THROW(static_cast<void>(Sample(Timestamp(0), nan)), std::invalid_argument);
}

TEST(SampleTest, RejectsInfiniteValues) {
    const double infinity = std::numeric_limits<double>::infinity();

    EXPECT_THROW(static_cast<void>(Sample(Timestamp(0), infinity)), std::invalid_argument);

    EXPECT_THROW(static_cast<void>(Sample(Timestamp(0), -infinity)), std::invalid_argument);
}

TEST(SampleTest, ComparesTimestampAndValue) {
    const Sample original(Timestamp(123), 72.5);
    const Sample same(Timestamp(123), 72.5);
    const Sample differentTime(Timestamp(124), 72.5);
    const Sample differentValue(Timestamp(123), 73.5);

    EXPECT_EQ(original, same);
    EXPECT_NE(original, differentTime);
    EXPECT_NE(original, differentValue);
}
