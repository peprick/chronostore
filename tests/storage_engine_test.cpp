#include "internal/storage_engine.hpp"
#include "internal/wal_recovery.hpp"
#include <gtest/gtest.h>

#include "internal/wal_record.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <bit>
#include <cstddef>
#include <fstream>
#include <stdexcept>

namespace chronostore::internal {
namespace {

class StorageEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        const ::testing::TestInfo* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();

        const std::string directory_name = "chronostore-storage-" + std::string(test_info->name());
        directory_ = std::filesystem::path(::testing::TempDir()) / directory_name;
        path_ = directory_ / "chronostore.wal";

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

SeriesKey temperature_series() {
    return SeriesKey{"temperature", {Tag{"room", "lab"}}};
}

void append_prefix_to_file(const std::filesystem::path& path,
                           const std::vector<std::uint8_t>& bytes, std::size_t prefix_size) {
    if (prefix_size > bytes.size()) {
        throw std::invalid_argument("prefix size exceeds byte vector size");
    }

    std::ofstream output(path, std::ios::binary | std::ios::app);

    if (!output.is_open()) {
        throw std::runtime_error("failed to open storage-engine test WAL");
    }

    for (std::size_t index = 0; index < prefix_size; ++index) {
        output.put(std::bit_cast<char>(bytes[index]));
    }

    output.close();

    if (!output) {
        throw std::runtime_error("failed to append storage-engine test WAL");
    }
}

TEST_F(StorageEngineTest, NewEngineCreatesEmptyWal) {
    EXPECT_FALSE(std::filesystem::exists(path_));

    StorageEngine engine(path_, WalDurability::buffered);

    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_TRUE(engine.empty());
    EXPECT_EQ(engine.sample_count(), 0U);
    EXPECT_EQ(engine.wal_size_bytes(), 0U);
}

TEST_F(StorageEngineTest, PutMakesSampleImmediatelyQueryable) {
    StorageEngine engine(path_, WalDurability::buffered);

    const SeriesKey series = temperature_series();

    const Sample sample{Timestamp{100}, 21.5};

    engine.put(series, sample);

    EXPECT_FALSE(engine.empty());
    EXPECT_EQ(engine.sample_count(), 1U);
    EXPECT_GT(engine.wal_size_bytes(), 0U);

    const std::optional<Sample> latest = engine.latest(series);

    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest.value(), sample);

    const std::vector<Sample> samples = engine.range(series, Timestamp{0}, Timestamp{200});

    ASSERT_EQ(samples.size(), 1U);
    EXPECT_EQ(samples[0], sample);

    engine.sync();
}

TEST_F(StorageEngineTest, ReopenRecoversSamplesFromWal) {
    const SeriesKey series = temperature_series();

    const Sample first{Timestamp{100}, 21.5};

    const Sample second{Timestamp{200}, 22.75};

    std::uint64_t wal_size_before_restart = 0;

    {
        StorageEngine engine(path_, WalDurability::buffered);

        engine.put(series, first);
        engine.put(series, second);
        engine.sync();

        wal_size_before_restart = engine.wal_size_bytes();
    }

    {
        StorageEngine restarted(path_, WalDurability::buffered);

        EXPECT_FALSE(restarted.empty());
        EXPECT_EQ(restarted.sample_count(), 2U);

        EXPECT_EQ(restarted.wal_size_bytes(), wal_size_before_restart);

        const std::optional<Sample> latest = restarted.latest(series);

        ASSERT_TRUE(latest.has_value());
        EXPECT_EQ(latest.value(), second);

        const std::vector<Sample> samples = restarted.range(series, Timestamp{100}, Timestamp{201});

        ASSERT_EQ(samples.size(), 2U);
        EXPECT_EQ(samples[0], first);
        EXPECT_EQ(samples[1], second);
    }
}
TEST_F(StorageEngineTest, OverwriteAtSameTimestampSurvivesRestart) {
    const SeriesKey series = temperature_series();

    const Sample original{Timestamp{100}, 21.5};

    const Sample replacement{Timestamp{100}, 24.0};

    {
        StorageEngine engine(path_, WalDurability::buffered);

        engine.put(series, original);
        engine.put(series, replacement);
        engine.sync();

        EXPECT_EQ(engine.sample_count(), 1U);
    }

    {
        StorageEngine restarted(path_, WalDurability::buffered);

        EXPECT_EQ(restarted.sample_count(), 1U);

        const std::optional<Sample> latest = restarted.latest(series);

        ASSERT_TRUE(latest.has_value());
        EXPECT_EQ(latest.value(), replacement);

        const std::vector<Sample> samples = restarted.range(series, Timestamp{100}, Timestamp{101});

        ASSERT_EQ(samples.size(), 1U);
        EXPECT_EQ(samples[0], replacement);
    }
}

TEST_F(StorageEngineTest, StartupRepairsIncompleteWalTail) {
    const SeriesKey series = temperature_series();

    const Sample first{Timestamp{100}, 21.5};

    const Sample interrupted{Timestamp{200}, 99.0};

    const Sample after_repair{Timestamp{300}, 23.5};

    std::uint64_t valid_wal_size = 0;

    {
        StorageEngine engine(path_, WalDurability::buffered);

        engine.put(series, first);
        engine.sync();

        valid_wal_size = engine.wal_size_bytes();
    }

    const WalPutRecord interrupted_record{series, interrupted};

    const std::vector<std::uint8_t> encoded = encode_wal_record(interrupted_record);

    ASSERT_GT(encoded.size(), 1U);

    const std::size_t partial_size = encoded.size() / 2U;

    append_prefix_to_file(path_, encoded, partial_size);

    const std::uint64_t damaged_wal_size =
        static_cast<std::uint64_t>(std::filesystem::file_size(path_));

    EXPECT_EQ(damaged_wal_size, valid_wal_size + static_cast<std::uint64_t>(partial_size));

    {
        StorageEngine recovered(path_, WalDurability::buffered);

        EXPECT_EQ(recovered.sample_count(), 1U);
        EXPECT_EQ(recovered.wal_size_bytes(), valid_wal_size);

        const std::uint64_t repaired_file_size =
            static_cast<std::uint64_t>(std::filesystem::file_size(path_));

        EXPECT_EQ(repaired_file_size, valid_wal_size);

        recovered.put(series, after_repair);
        recovered.sync();
    }

    {
        StorageEngine restarted(path_, WalDurability::buffered);

        EXPECT_EQ(restarted.sample_count(), 2U);

        const std::optional<Sample> latest = restarted.latest(series);

        ASSERT_TRUE(latest.has_value());
        EXPECT_EQ(latest.value(), after_repair);

        const std::vector<Sample> samples = restarted.range(series, Timestamp{100}, Timestamp{301});

        ASSERT_EQ(samples.size(), 2U);
        EXPECT_EQ(samples[0], first);
        EXPECT_EQ(samples[1], after_repair);
    }
}
TEST_F(StorageEngineTest, StartupRejectsCorruptedCompleteRecord) {
    const SeriesKey series = temperature_series();

    const WalPutRecord first{series, Sample{Timestamp{100}, 21.5}};

    const WalPutRecord second{series, Sample{Timestamp{200}, 22.5}};

    const std::vector<std::uint8_t> first_bytes = encode_wal_record(first);

    std::vector<std::uint8_t> second_bytes = encode_wal_record(second);

    constexpr std::size_t checksum_size = sizeof(std::uint32_t);

    ASSERT_GT(second_bytes.size(), checksum_size);

    const std::size_t corrupted_payload_index = second_bytes.size() - checksum_size - 1U;

    second_bytes[corrupted_payload_index] =
        static_cast<std::uint8_t>(second_bytes[corrupted_payload_index] ^ 0x01U);

    std::vector<std::uint8_t> file_bytes;

    file_bytes.reserve(first_bytes.size() + second_bytes.size());

    for (std::uint8_t byte : first_bytes) {
        file_bytes.push_back(byte);
    }

    for (std::uint8_t byte : second_bytes) {
        file_bytes.push_back(byte);
    }

    append_prefix_to_file(path_, file_bytes, file_bytes.size());

    const std::uint64_t size_before_startup =
        static_cast<std::uint64_t>(std::filesystem::file_size(path_));

    bool corruption_was_rejected = false;

    try {
        StorageEngine engine(path_, WalDurability::buffered);

        static_cast<void>(engine);
    } catch (const WalFormatError& error) {
        corruption_was_rejected = true;

        EXPECT_EQ(error.offset(), first_bytes.size());

        EXPECT_EQ(error.decode_error(), WalDecodeError::checksum_mismatch);
    }

    EXPECT_TRUE(corruption_was_rejected);

    const std::uint64_t size_after_startup =
        static_cast<std::uint64_t>(std::filesystem::file_size(path_));

    EXPECT_EQ(size_after_startup, size_before_startup);
}

TEST_F(StorageEngineTest, FlushPublishesSegmentAndResetsWal) {
    const SeriesKey series = temperature_series();
    std::vector<Sample> expected;

    {
        StorageEngine engine(path_, WalDurability::buffered);

        for (std::int64_t timestamp = 0; timestamp < 300; ++timestamp) {
            const Sample sample{Timestamp{timestamp}, static_cast<double>(timestamp)};
            expected.push_back(sample);
            engine.put(series, sample);
        }

        engine.sync();
        ASSERT_GT(engine.wal_size_bytes(), 0U);
        engine.flush();

        EXPECT_EQ(engine.wal_size_bytes(), 0U);
        EXPECT_EQ(engine.segment_count(), 1U);
        EXPECT_EQ(engine.sample_count(), expected.size());
        EXPECT_EQ(engine.range(series, Timestamp{0}, Timestamp{300}), expected);
        EXPECT_TRUE(std::filesystem::is_regular_file(manifest_path(directory_)));
    }

    const StorageEngine reopened(path_, WalDurability::buffered);
    EXPECT_EQ(reopened.segment_count(), 1U);
    EXPECT_EQ(reopened.sample_count(), expected.size());
    EXPECT_EQ(reopened.range(series, Timestamp{0}, Timestamp{300}), expected);
}

TEST_F(StorageEngineTest, NewerWalAndSegmentsOverrideOlderValues) {
    const SeriesKey series = temperature_series();

    {
        StorageEngine engine(path_, WalDurability::buffered);
        engine.put(series, Sample{Timestamp{100}, 1.0});
        engine.flush();
        engine.put(series, Sample{Timestamp{100}, 2.0});
        engine.put(series, Sample{Timestamp{200}, 3.0});
        engine.sync();

        EXPECT_EQ(engine.sample_count(), 2U);
        EXPECT_EQ(engine.range(series, Timestamp{100}, Timestamp{201}),
                  (std::vector<Sample>{Sample{Timestamp{100}, 2.0}, Sample{Timestamp{200}, 3.0}}));
    }

    {
        StorageEngine reopened(path_, WalDurability::buffered);
        EXPECT_EQ(reopened.sample_count(), 2U);
        EXPECT_EQ(reopened.latest(series).value(), (Sample{Timestamp{200}, 3.0}));
        reopened.flush();
        EXPECT_EQ(reopened.segment_count(), 2U);
    }

    const StorageEngine reopened_again(path_, WalDurability::buffered);
    EXPECT_EQ(reopened_again.sample_count(), 2U);
    EXPECT_EQ(reopened_again.range(series, Timestamp{100}, Timestamp{201}),
              (std::vector<Sample>{Sample{Timestamp{100}, 2.0}, Sample{Timestamp{200}, 3.0}}));
}

TEST_F(StorageEngineTest, CompactionMergesSegmentsAndRemovesObsoleteFiles) {
    const SeriesKey series = temperature_series();

    StorageEngine engine(path_, WalDurability::buffered);
    engine.put(series, Sample{Timestamp{10}, 1.0});
    engine.put(series, Sample{Timestamp{20}, 2.0});
    engine.flush();
    engine.put(series, Sample{Timestamp{20}, 22.0});
    engine.put(series, Sample{Timestamp{30}, 3.0});
    engine.flush();
    ASSERT_EQ(engine.segment_count(), 2U);

    engine.compact();

    EXPECT_EQ(engine.segment_count(), 1U);
    EXPECT_EQ(engine.sample_count(), 3U);
    EXPECT_EQ(engine.range(series, Timestamp{0}, Timestamp{40}),
              (std::vector<Sample>{Sample{Timestamp{10}, 1.0}, Sample{Timestamp{20}, 22.0},
                                   Sample{Timestamp{30}, 3.0}}));

    std::size_t segment_files = 0;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory_)) {
        if (entry.path().extension() == ".cst") {
            ++segment_files;
        }
    }

    EXPECT_EQ(segment_files, 1U);
}

TEST_F(StorageEngineTest, RecoveryDoesNotDoubleCountWalAlreadyPublishedInManifest) {
    const SeriesKey series = temperature_series();
    const Sample sample{Timestamp{100}, 9.5};

    {
        StorageEngine engine(path_, WalDurability::buffered);
        engine.put(series, sample);
        engine.sync();
    }

    const std::string filename = "segment-00000000000000000001.cst";
    static_cast<void>(write_segment_file(directory_ / filename,
                                         {SegmentBlock{series, std::vector<Sample>{sample}}}));
    store_manifest(directory_, ManifestState{1U, 1U, {filename}});

    const StorageEngine recovered(path_, WalDurability::buffered);
    EXPECT_EQ(recovered.segment_count(), 1U);
    EXPECT_EQ(recovered.sample_count(), 1U);
    EXPECT_EQ(recovered.latest(series).value(), sample);
    EXPECT_GT(recovered.wal_size_bytes(), 0U);
}

} // namespace
} // namespace chronostore::internal
