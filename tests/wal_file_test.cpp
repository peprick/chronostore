#include "internal/wal_file.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <bit>
#include <chrono>
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

class TemporaryWalPath {
public:
    TemporaryWalPath() {
        static std::atomic<std::uint64_t> next_id{0};

        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

        const std::uint64_t id = next_id.fetch_add(1);

        const std::string filename =
            "chronostore-wal-" + std::to_string(timestamp) + "-" + std::to_string(id) + ".log";

        path_ = std::filesystem::temp_directory_path() / filename;
    }

    ~TemporaryWalPath() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]]
    const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);

    if (!input.is_open()) {
        throw std::runtime_error("failed to open test WAL");
    }

    std::vector<std::uint8_t> bytes;

    while (true) {
        char character = 0;
        input.get(character);

        if (input.eof()) {
            break;
        }

        if (!input) {
            throw std::runtime_error("failed to read test WAL");
        }

        bytes.push_back(std::bit_cast<std::uint8_t>(character));
    }

    return bytes;
}

WalPutRecord first_record() {
    return WalPutRecord{SeriesKey{"temperature", {Tag{"room", "lab"}}},
                        Sample{Timestamp{100}, 21.5}};
}

WalPutRecord second_record() {
    return WalPutRecord{SeriesKey{"pressure", {Tag{"sensor", "p1"}}},
                        Sample{Timestamp{200}, 101.25}};
}

TEST(WalWriterTest, AppendsRecordToNewFile) {
    TemporaryWalPath temporary;
    const WalPutRecord record = first_record();

    const std::vector<std::uint8_t> expected = encode_wal_record(record);

    {
        WalWriter writer(temporary.path(), WalDurability::buffered);

        EXPECT_EQ(writer.path(), temporary.path());
        EXPECT_EQ(writer.size_bytes(), 0U);

        writer.append(record);

        EXPECT_EQ(writer.size_bytes(), expected.size());

        writer.sync();
    }

    const std::vector<std::uint8_t> persisted = read_file_bytes(temporary.path());

    EXPECT_EQ(persisted, expected);

    const WalDecodeResult decoded = decode_wal_record(persisted);

    ASSERT_TRUE(decoded.success());
    ASSERT_TRUE(decoded.record.has_value());
    EXPECT_EQ(decoded.record.value(), record);
}

TEST(WalWriterTest, ReopenAppendsWithoutOverwriting) {
    TemporaryWalPath temporary;

    const WalPutRecord first = first_record();
    const WalPutRecord second = second_record();

    const std::vector<std::uint8_t> first_bytes = encode_wal_record(first);

    const std::vector<std::uint8_t> second_bytes = encode_wal_record(second);

    {
        WalWriter writer(temporary.path(), WalDurability::buffered);

        writer.append(first);
        writer.sync();
    }

    {
        WalWriter writer(temporary.path(), WalDurability::buffered);

        EXPECT_EQ(writer.size_bytes(), first_bytes.size());

        writer.append(second);
        writer.sync();

        EXPECT_EQ(writer.size_bytes(), first_bytes.size() + second_bytes.size());
    }

    const std::vector<std::uint8_t> persisted = read_file_bytes(temporary.path());

    const std::span<const std::uint8_t> stream{persisted};

    const WalDecodeResult first_result = decode_wal_record(stream);

    ASSERT_TRUE(first_result.success());
    ASSERT_TRUE(first_result.record.has_value());
    EXPECT_EQ(first_result.record.value(), first);

    const std::span<const std::uint8_t> remaining = stream.subspan(first_result.bytes_consumed);

    const WalDecodeResult second_result = decode_wal_record(remaining);

    ASSERT_TRUE(second_result.success());
    ASSERT_TRUE(second_result.record.has_value());
    EXPECT_EQ(second_result.record.value(), second);

    EXPECT_EQ(first_result.bytes_consumed + second_result.bytes_consumed, persisted.size());
}

TEST(WalWriterTest, SyncOnAppendUsesDefaultDurability) {
    TemporaryWalPath temporary;
    const WalPutRecord record = first_record();

    {
        WalWriter writer(temporary.path());
        writer.append(record);

        EXPECT_EQ(writer.size_bytes(), encode_wal_record(record).size());
    }

    const std::vector<std::uint8_t> persisted = read_file_bytes(temporary.path());

    const WalDecodeResult result = decode_wal_record(persisted);

    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.record.has_value());
    EXPECT_EQ(result.record.value(), record);
}

TEST(WalWriterTest, ResetDurablyTruncatesWalAndAllowsNewAppend) {
    TemporaryWalPath temporary;
    const WalPutRecord first = first_record();
    const WalPutRecord second = second_record();

    {
        WalWriter writer(temporary.path(), WalDurability::buffered);
        writer.append(first);
        writer.sync();
        ASSERT_GT(writer.size_bytes(), 0U);

        writer.reset();
        EXPECT_EQ(writer.size_bytes(), 0U);
        EXPECT_EQ(std::filesystem::file_size(temporary.path()), 0U);

        writer.append(second);
        writer.sync();
    }

    const std::vector<std::uint8_t> persisted = read_file_bytes(temporary.path());
    const WalDecodeResult decoded = decode_wal_record(persisted);
    ASSERT_TRUE(decoded.success());
    EXPECT_EQ(decoded.record.value(), second);
    EXPECT_EQ(decoded.bytes_consumed, persisted.size());
}

} // namespace
} // namespace chronostore::internal
