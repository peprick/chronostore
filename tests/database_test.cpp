#include <chronostore/database.hpp>

#include <gtest/gtest.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace chronostore {
namespace {

static_assert(!std::is_copy_constructible_v<Database>);

static_assert(!std::is_copy_assignable_v<Database>);

static_assert(std::is_nothrow_move_constructible_v<Database>);

static_assert(std::is_nothrow_move_assignable_v<Database>);

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        const ::testing::TestInfo* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();

        const std::string directory_name = "chronostore-database-" + std::string(test_info->name());

        directory_ = std::filesystem::path(::testing::TempDir()) / directory_name;

        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path directory_;
};

DatabaseOptions buffered_options() {
    DatabaseOptions options;
    options.durability = Durability::buffered;

    return options;
}

SeriesKey public_temperature_series() {
    return SeriesKey{"temperature", {Tag{"room", "lab"}}};
}

TEST_F(DatabaseTest, CreatesDatabaseDirectoryAndWal) {
    EXPECT_FALSE(std::filesystem::exists(directory_));

    Database database(directory_, buffered_options());

    EXPECT_TRUE(std::filesystem::is_directory(directory_));

    EXPECT_TRUE(std::filesystem::is_regular_file(directory_ / "chronostore.wal"));

    EXPECT_TRUE(database.empty());
    EXPECT_EQ(database.sample_count(), 0U);
}

TEST_F(DatabaseTest, WritesQueriesAndRecoversAfterReopen) {
    const SeriesKey series = public_temperature_series();

    const Sample first{Timestamp{100}, 21.5};

    const Sample second{Timestamp{200}, 22.75};

    {
        Database database(directory_, buffered_options());

        database.put(series, first);
        database.put(series, second);
        database.sync();

        EXPECT_EQ(database.sample_count(), 2U);
    }

    {
        Database reopened(directory_, buffered_options());

        EXPECT_EQ(reopened.sample_count(), 2U);

        const std::optional<Sample> latest = reopened.latest(series);

        ASSERT_TRUE(latest.has_value());
        EXPECT_EQ(latest.value(), second);

        EXPECT_EQ(reopened.get(series, Timestamp{100}), first);
        EXPECT_EQ(reopened.get(series, Timestamp{101}), std::nullopt);

        const std::vector<Sample> samples = reopened.range(series, Timestamp{100}, Timestamp{201});

        ASSERT_EQ(samples.size(), 2U);
        EXPECT_EQ(samples[0], first);
        EXPECT_EQ(samples[1], second);
    }
}

TEST_F(DatabaseTest, RejectsPathThatIsARegularFile) {
    std::ofstream output(directory_, std::ios::binary | std::ios::trunc);

    ASSERT_TRUE(output.is_open());

    output << "not a database directory";
    output.close();

    ASSERT_TRUE(output);

    EXPECT_THROW(
        {
            Database database(directory_);
            static_cast<void>(database);
        },
        std::invalid_argument);
}

TEST_F(DatabaseTest, MoveTransfersDatabaseOwnership) {
    const SeriesKey series = public_temperature_series();

    const Sample sample{Timestamp{500}, 25.0};

    {
        Database original(directory_, buffered_options());

        original.put(series, sample);

        Database moved(std::move(original));

        EXPECT_THROW(static_cast<void>(original.empty()), std::logic_error);

        EXPECT_EQ(moved.sample_count(), 1U);

        const std::optional<Sample> latest = moved.latest(series);

        ASSERT_TRUE(latest.has_value());
        EXPECT_EQ(latest.value(), sample);

        moved.sync();
    }

    Database reopened(directory_, buffered_options());

    EXPECT_EQ(reopened.sample_count(), 1U);
}

TEST_F(DatabaseTest, AutomaticFlushPublishesSegmentAtThreshold) {
    DatabaseOptions options = buffered_options();
    options.memtable_flush_threshold_samples = 2U;
    Database database(directory_, options);
    const SeriesKey series = public_temperature_series();

    database.put(series, Sample{Timestamp{1}, 10.0});
    DatabaseStats stats = database.stats();
    EXPECT_EQ(stats.sample_count, 1U);
    EXPECT_EQ(stats.memtable_sample_count, 1U);
    EXPECT_EQ(stats.segment_count, 0U);

    database.put(series, Sample{Timestamp{2}, 11.0});
    stats = database.stats();
    EXPECT_EQ(stats.sample_count, 2U);
    EXPECT_EQ(stats.memtable_sample_count, 0U);
    EXPECT_EQ(stats.segment_count, 1U);
    EXPECT_EQ(stats.wal_size_bytes, 0U);
}

TEST_F(DatabaseTest, ManualFlushAndCompactionArePublicOperations) {
    DatabaseOptions options = buffered_options();
    options.memtable_flush_threshold_samples = 0U;
    Database database(directory_, options);
    const SeriesKey series = public_temperature_series();

    database.put(series, Sample{Timestamp{1}, 1.0});
    database.flush();
    database.put(series, Sample{Timestamp{1}, 2.0});
    database.put(series, Sample{Timestamp{2}, 3.0});
    database.flush();
    ASSERT_EQ(database.stats().segment_count, 2U);

    database.compact();
    const DatabaseStats stats = database.stats();
    EXPECT_EQ(stats.segment_count, 1U);
    EXPECT_EQ(stats.sample_count, 2U);
    EXPECT_EQ(database.range(series, Timestamp{1}, Timestamp{3}),
              (std::vector<Sample>{Sample{Timestamp{1}, 2.0}, Sample{Timestamp{2}, 3.0}}));
}

TEST_F(DatabaseTest, ListsUniqueSeriesAcrossMemoryAndSegments) {
    DatabaseOptions options = buffered_options();
    options.memtable_flush_threshold_samples = 0U;
    Database database(directory_, options);
    const SeriesKey first{"cpu", {Tag{"host", "a"}}};
    const SeriesKey second{"temperature", {Tag{"room", "lab"}}};

    database.put(first, Sample{Timestamp{1}, 10.0});
    database.flush();
    database.put(first, Sample{Timestamp{2}, 11.0});
    database.put(second, Sample{Timestamp{1}, 21.5});

    EXPECT_EQ(database.series(), (std::vector<SeriesKey>{first, second}));
}

TEST_F(DatabaseTest, RejectsSecondOwnerOfSameDirectory) {
    Database first(directory_, buffered_options());

    EXPECT_THROW(
        {
            Database second(directory_, buffered_options());
            static_cast<void>(second);
        },
        DatabaseBusyError);
}

TEST_F(DatabaseTest, ReportsCorruptWalThroughPublicErrorType) {
    {
        Database database(directory_, buffered_options());
        database.put(public_temperature_series(), Sample{Timestamp{100}, 21.5});
        database.sync();
    }

    std::fstream wal(directory_ / "chronostore.wal",
                     std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(wal.is_open());
    wal.seekg(-1, std::ios::end);
    char final_byte = 0;
    wal.read(&final_byte, 1);
    ASSERT_TRUE(wal);
    final_byte = static_cast<char>(static_cast<unsigned char>(final_byte) ^ 1U);
    wal.seekp(-1, std::ios::end);
    wal.write(&final_byte, 1);
    wal.close();
    ASSERT_TRUE(wal);

    EXPECT_THROW(
        {
            Database corrupted(directory_, buffered_options());
            static_cast<void>(corrupted);
        },
        DatabaseCorruptionError);
}

TEST_F(DatabaseTest, SerializesConcurrentWriters) {
    DatabaseOptions options = buffered_options();
    options.memtable_flush_threshold_samples = 0U;
    Database database(directory_, options);
    const SeriesKey series = public_temperature_series();
    constexpr std::size_t writer_count = 4U;
    constexpr std::size_t samples_per_writer = 100U;
    std::vector<std::thread> writers;
    std::mutex error_mutex;
    std::exception_ptr thread_error;

    for (std::size_t writer = 0; writer < writer_count; ++writer) {
        writers.emplace_back([&, writer] {
            try {
                for (std::size_t index = 0; index < samples_per_writer; ++index) {
                    const auto timestamp =
                        static_cast<std::int64_t>(writer * samples_per_writer + index);
                    database.put(series,
                                 Sample{Timestamp{timestamp}, static_cast<double>(timestamp)});
                }
            } catch (...) {
                std::lock_guard lock(error_mutex);
                thread_error = std::current_exception();
            }
        });
    }

    for (std::thread& writer : writers) {
        writer.join();
    }

    if (thread_error) {
        std::rethrow_exception(thread_error);
    }

    EXPECT_EQ(database.sample_count(), writer_count * samples_per_writer);
    const std::vector<Sample> samples = database.range(series, Timestamp{0}, Timestamp{400});
    ASSERT_EQ(samples.size(), writer_count * samples_per_writer);
    EXPECT_EQ(samples.front(), (Sample{Timestamp{0}, 0.0}));
    EXPECT_EQ(samples.back(), (Sample{Timestamp{399}, 399.0}));
}

} // namespace
} // namespace chronostore
