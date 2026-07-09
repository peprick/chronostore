#include "internal/wal_file.hpp"
#include "internal/wal_recovery.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace chronostore::internal {
namespace {

class WalRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        const ::testing::TestInfo* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();

        const std::string filename = "chronostore-" + std::string(test_info->name()) + ".wal";

        path_ = std::filesystem::path(::testing::TempDir()) / filename;

        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    std::filesystem::path path_;
};

void write_file_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output.is_open()) {
        throw std::runtime_error("failed to open recovery test file");
    }

    for (std::uint8_t byte : bytes) {
        output.put(std::bit_cast<char>(byte));
    }

    output.close();

    if (!output) {
        throw std::runtime_error("failed to write recovery test file");
    }
}

WalPutRecord recovery_first_record() {
    return WalPutRecord{SeriesKey{"temperature", {Tag{"room", "lab"}}},
                        Sample{Timestamp{100}, 21.5}};
}

WalPutRecord recovery_second_record() {
    return WalPutRecord{SeriesKey{"pressure", {Tag{"sensor", "p1"}}},
                        Sample{Timestamp{200}, 101.25}};
}

TEST_F(WalRecoveryTest, EmptyFileReplaysNothing) {
    {
        WalWriter writer(path_, WalDurability::buffered);
    }

    std::vector<WalPutRecord> replayed;

    const WalReplayResult result =
        replay_wal(path_, [&replayed](const WalPutRecord& record) { replayed.push_back(record); });

    EXPECT_TRUE(replayed.empty());
    EXPECT_EQ(result.records_replayed, 0U);
    EXPECT_EQ(result.valid_bytes, 0U);
    EXPECT_EQ(result.incomplete_tail_bytes, 0U);
    EXPECT_FALSE(result.had_incomplete_tail());
}

TEST_F(WalRecoveryTest, ReplaysMultipleRecordsInOrder) {
    const WalPutRecord first = recovery_first_record();

    const WalPutRecord second = recovery_second_record();

    std::uint64_t expected_size = 0;

    {
        WalWriter writer(path_, WalDurability::buffered);

        writer.append(first);
        writer.append(second);
        writer.sync();

        expected_size = writer.size_bytes();
    }

    std::vector<WalPutRecord> replayed;

    const WalReplayResult result =
        replay_wal(path_, [&replayed](const WalPutRecord& record) { replayed.push_back(record); });

    const std::vector<WalPutRecord> expected{first, second};

    EXPECT_EQ(replayed, expected);
    EXPECT_EQ(result.records_replayed, 2U);
    EXPECT_EQ(result.valid_bytes, expected_size);
    EXPECT_EQ(result.incomplete_tail_bytes, 0U);
    EXPECT_FALSE(result.had_incomplete_tail());
}

TEST_F(WalRecoveryTest, ReplaysRecordLargerThanReadChunk) {
    const std::string large_tag_value(70U * 1024U, 'x');

    const WalPutRecord original{SeriesKey{"large-record", {Tag{"payload", large_tag_value}}},
                                Sample{Timestamp{300}, 8.5}};

    const std::vector<std::uint8_t> encoded = encode_wal_record(original);

    EXPECT_GT(encoded.size(), 64U * 1024U);

    {
        WalWriter writer(path_, WalDurability::buffered);

        writer.append(original);
        writer.sync();
    }

    std::vector<WalPutRecord> replayed;

    const WalReplayResult result =
        replay_wal(path_, [&replayed](const WalPutRecord& record) { replayed.push_back(record); });

    ASSERT_EQ(replayed.size(), 1U);
    EXPECT_EQ(replayed.front(), original);
    EXPECT_EQ(result.records_replayed, 1U);
    EXPECT_EQ(result.valid_bytes, encoded.size());
    EXPECT_FALSE(result.had_incomplete_tail());
}

TEST_F(WalRecoveryTest, IgnoresIncompleteFinalRecord) {
    const WalPutRecord first = recovery_first_record();

    const WalPutRecord second = recovery_second_record();

    const std::vector<std::uint8_t> first_bytes = encode_wal_record(first);

    const std::vector<std::uint8_t> second_bytes = encode_wal_record(second);

    const std::size_t partial_size = second_bytes.size() / 2U;

    std::vector<std::uint8_t> file_bytes = first_bytes;

    for (std::size_t index = 0; index < partial_size; ++index) {
        file_bytes.push_back(second_bytes[index]);
    }

    write_file_bytes(path_, file_bytes);

    std::vector<WalPutRecord> replayed;

    const WalReplayResult result =
        replay_wal(path_, [&replayed](const WalPutRecord& record) { replayed.push_back(record); });

    ASSERT_EQ(replayed.size(), 1U);
    EXPECT_EQ(replayed.front(), first);

    EXPECT_EQ(result.records_replayed, 1U);
    EXPECT_EQ(result.valid_bytes, first_bytes.size());
    EXPECT_EQ(result.incomplete_tail_bytes, partial_size);
    EXPECT_TRUE(result.had_incomplete_tail());
}

TEST_F(WalRecoveryTest, ReportsCorruptionAtExactRecordOffset) {
    const WalPutRecord first = recovery_first_record();

    const WalPutRecord second = recovery_second_record();

    const std::vector<std::uint8_t> first_bytes = encode_wal_record(first);

    const std::vector<std::uint8_t> second_bytes = encode_wal_record(second);

    std::vector<std::uint8_t> file_bytes = first_bytes;

    for (std::uint8_t byte : second_bytes) {
        file_bytes.push_back(byte);
    }

    constexpr std::size_t payload_byte_offset = 20;

    const std::size_t corrupted_byte = first_bytes.size() + payload_byte_offset;

    file_bytes[corrupted_byte] = static_cast<std::uint8_t>(file_bytes[corrupted_byte] ^ 0x01U);

    write_file_bytes(path_, file_bytes);

    std::vector<WalPutRecord> replayed;

    try {
        static_cast<void>(replay_wal(
            path_, [&replayed](const WalPutRecord& record) { replayed.push_back(record); }));

        FAIL() << "expected WalFormatError";
    } catch (const WalFormatError& error) {
        EXPECT_EQ(error.offset(), first_bytes.size());

        EXPECT_EQ(error.decode_error(), WalDecodeError::checksum_mismatch);
    }

    ASSERT_EQ(replayed.size(), 1U);
    EXPECT_EQ(replayed.front(), first);
}

TEST_F(WalRecoveryTest, PropagatesReplayCallbackFailure) {
    {
        WalWriter writer(path_, WalDurability::buffered);

        writer.append(recovery_first_record());
        writer.append(recovery_second_record());
        writer.sync();
    }

    std::size_t callback_count = 0;

    try {
        static_cast<void>(replay_wal(path_, [&callback_count](const WalPutRecord&) {
            ++callback_count;

            throw std::runtime_error("callback failed");
        }));

        FAIL() << "expected callback exception";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "callback failed");
    }

    EXPECT_EQ(callback_count, 1U);
}

TEST_F(WalRecoveryTest, MissingFileReportsSystemError) {
    EXPECT_FALSE(std::filesystem::exists(path_));

    try {
        static_cast<void>(replay_wal(path_, [](const WalPutRecord&) {}));

        FAIL() << "expected std::system_error";
    } catch (const std::system_error& error) {
        EXPECT_NE(error.code().value(), 0);
    }
}

    TEST_F(WalRecoveryTest, RepairsTailAndAllowsNewAppend) {
    const WalPutRecord first =
        recovery_first_record();

    const WalPutRecord second =
        recovery_second_record();

    const std::vector<std::uint8_t> first_bytes =
        encode_wal_record(first);

    const std::vector<std::uint8_t> second_bytes =
        encode_wal_record(second);

    const std::size_t partial_size =
        second_bytes.size() / 2U;

    std::vector<std::uint8_t> damaged_file =
        first_bytes;

    for (
        std::size_t index = 0;
        index < partial_size;
        ++index
    ) {
        damaged_file.push_back(second_bytes[index]);
    }

    write_file_bytes(path_, damaged_file);

    std::vector<WalPutRecord> initial_replay;

    const WalReplayResult repair_point = replay_wal(
        path_,
        [&initial_replay](const WalPutRecord& record) {
            initial_replay.push_back(record);
        }
    );

    ASSERT_TRUE(repair_point.had_incomplete_tail());
    ASSERT_EQ(initial_replay.size(), 1U);
    EXPECT_EQ(initial_replay.front(), first);

    repair_wal_tail(path_, repair_point);

    EXPECT_EQ(
        std::filesystem::file_size(path_),
        first_bytes.size()
    );

    std::vector<WalPutRecord> after_repair;

    const WalReplayResult clean_result = replay_wal(
        path_,
        [&after_repair](const WalPutRecord& record) {
            after_repair.push_back(record);
        }
    );

    ASSERT_EQ(after_repair.size(), 1U);
    EXPECT_EQ(after_repair.front(), first);
    EXPECT_FALSE(clean_result.had_incomplete_tail());

    {
        WalWriter writer(
            path_,
            WalDurability::buffered
        );

        EXPECT_EQ(
            writer.size_bytes(),
            first_bytes.size()
        );

        writer.append(second);
        writer.sync();
    }

    std::vector<WalPutRecord> final_replay;

    const WalReplayResult final_result = replay_wal(
        path_,
        [&final_replay](const WalPutRecord& record) {
            final_replay.push_back(record);
        }
    );

    const std::vector<WalPutRecord> expected{
        first,
        second
    };

    EXPECT_EQ(final_replay, expected);
    EXPECT_EQ(final_result.records_replayed, 2U);
    EXPECT_FALSE(final_result.had_incomplete_tail());
}

TEST_F(WalRecoveryTest, RepairIsNoOpForCleanWal) {
    const WalPutRecord record =
        recovery_first_record();

    {
        WalWriter writer(
            path_,
            WalDurability::buffered
        );

        writer.append(record);
        writer.sync();
    }

    const std::uintmax_t original_size =
        std::filesystem::file_size(path_);

    const WalReplayResult replay_result = replay_wal(
        path_,
        [](const WalPutRecord&) {
        }
    );

    ASSERT_FALSE(
        replay_result.had_incomplete_tail()
    );

    repair_wal_tail(path_, replay_result);

    EXPECT_EQ(
        std::filesystem::file_size(path_),
        original_size
    );
}

TEST_F(WalRecoveryTest, RefusesStaleRepairResult) {
    const std::vector<std::uint8_t> first_bytes =
        encode_wal_record(recovery_first_record());

    const std::vector<std::uint8_t> second_bytes =
        encode_wal_record(recovery_second_record());

    const std::size_t partial_size =
        second_bytes.size() / 2U;

    std::vector<std::uint8_t> damaged_file =
        first_bytes;

    for (
        std::size_t index = 0;
        index < partial_size;
        ++index
    ) {
        damaged_file.push_back(second_bytes[index]);
    }

    write_file_bytes(path_, damaged_file);

    const WalReplayResult stale_result = replay_wal(
        path_,
        [](const WalPutRecord&) {
        }
    );

    ASSERT_TRUE(stale_result.had_incomplete_tail());

    const std::uintmax_t changed_size =
        std::filesystem::file_size(path_) + 1U;

    std::filesystem::resize_file(
        path_,
        changed_size
    );

    EXPECT_THROW(
        repair_wal_tail(path_, stale_result),
        std::runtime_error
    );

    EXPECT_EQ(
        std::filesystem::file_size(path_),
        changed_size
    );
}

} // namespace
} // namespace chronostore::internal