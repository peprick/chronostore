#ifndef CHRONOSTORE_INTERNAL_SEGMENT_FILE_HPP
#define CHRONOSTORE_INTERNAL_SEGMENT_FILE_HPP

#include "internal/segment_block.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace chronostore::internal {

class SegmentFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct SegmentIndexEntry {
    SeriesKey series;
    Timestamp first_timestamp;
    Timestamp last_timestamp;
    std::uint64_t block_offset;
    std::uint32_t block_size;
    std::uint32_t sample_count;

    bool operator==(const SegmentIndexEntry&) const = default;
};

struct SegmentFileMetadata {
    std::uint64_t file_size{0};
    std::uint64_t sample_count{0};
    std::uint32_t block_count{0};
};

[[nodiscard]] SegmentFileMetadata write_segment_file(const std::filesystem::path& path,
                                                     const std::vector<SegmentBlock>& blocks);

class SegmentReader {
public:
    explicit SegmentReader(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::uint64_t file_size() const noexcept;
    [[nodiscard]] std::uint64_t sample_count() const noexcept;
    [[nodiscard]] std::size_t block_count() const noexcept;
    [[nodiscard]] const std::vector<SegmentIndexEntry>& index() const noexcept;

    [[nodiscard]] std::optional<Sample> latest(const SeriesKey& series) const;
    [[nodiscard]] std::optional<Sample> get(const SeriesKey& series, Timestamp timestamp) const;
    [[nodiscard]] std::vector<Sample> range(const SeriesKey& series, Timestamp start,
                                            Timestamp end) const;
    [[nodiscard]] std::vector<SegmentBlock> blocks() const;

private:
    [[nodiscard]] SegmentBlock read_block(const SegmentIndexEntry& entry) const;

    std::filesystem::path path_;
    std::uint64_t file_size_{0};
    std::uint64_t sample_count_{0};
    std::vector<SegmentIndexEntry> index_;
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_SEGMENT_FILE_HPP
