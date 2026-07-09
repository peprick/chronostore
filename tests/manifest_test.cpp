#include "internal/manifest.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace chronostore::internal {
namespace {

class ManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        const ::testing::TestInfo* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        directory_ = std::filesystem::path(::testing::TempDir()) /
                     ("chronostore-manifest-" + std::string(test_info->name()));
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path directory_;
};

void corrupt_manifest_byte(const std::filesystem::path& path, std::uint64_t offset) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekg(static_cast<std::streamoff>(offset));
    char character = 0;
    file.get(character);
    ASSERT_TRUE(file.good());
    const std::uint8_t changed =
        static_cast<std::uint8_t>(std::bit_cast<std::uint8_t>(character) ^ 0x01U);
    file.seekp(static_cast<std::streamoff>(offset));
    file.put(std::bit_cast<char>(changed));
    file.close();
    ASSERT_TRUE(file.good());
}

TEST_F(ManifestTest, MissingManifestRepresentsEmptyState) {
    EXPECT_EQ(load_manifest(directory_), ManifestState{});
}

TEST_F(ManifestTest, StoresLoadsAndAtomicallyReplacesState) {
    const ManifestState first{1U, 5U, {"segment-0001.cst", "segment-0002.cst"}};
    store_manifest(directory_, first);
    EXPECT_EQ(load_manifest(directory_), first);
    EXPECT_FALSE(std::filesystem::exists(manifest_path(directory_).string() + ".tmp"));

    const ManifestState replacement{2U, 8U, {"segment-0003.cst"}};
    store_manifest(directory_, replacement);
    EXPECT_EQ(load_manifest(directory_), replacement);
}

TEST_F(ManifestTest, RejectsInvalidOrDuplicateSegmentNames) {
    EXPECT_THROW(store_manifest(directory_, ManifestState{1U, 0U, {"../outside.cst"}}),
                 std::invalid_argument);
    EXPECT_THROW(store_manifest(directory_, ManifestState{1U, 0U, {"same.cst", "same.cst"}}),
                 std::invalid_argument);
    EXPECT_THROW(store_manifest(directory_, ManifestState{1U, 0U, {"segment.dat"}}),
                 std::invalid_argument);
}

TEST_F(ManifestTest, DetectsCorruption) {
    store_manifest(directory_, ManifestState{7U, 3U, {"segment-0007.cst"}});
    corrupt_manifest_byte(manifest_path(directory_), 16U);
    EXPECT_THROW(static_cast<void>(load_manifest(directory_)), ManifestFormatError);
}

} // namespace
} // namespace chronostore::internal
