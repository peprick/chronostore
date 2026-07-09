#ifndef CHRONOSTORE_INTERNAL_SEGMENT_BLOCK_HPP
#define CHRONOSTORE_INTERNAL_SEGMENT_BLOCK_HPP

#include <chronostore/model.hpp>

#include <cstddef>
#include <vector>

namespace chronostore::internal {

inline constexpr std::size_t max_samples_per_segment_block = 256;

class SegmentBlock {
public:
    SegmentBlock(SeriesKey series, std::vector<Sample> samples);

    [[nodiscard]]
    const SeriesKey& series() const noexcept;

    [[nodiscard]]
    const std::vector<Sample>& samples() const noexcept;

    [[nodiscard]]
    std::size_t sample_count() const noexcept;

    [[nodiscard]]
    Timestamp first_timestamp() const noexcept;

    [[nodiscard]]
    Timestamp last_timestamp() const noexcept;

    bool operator==(const SegmentBlock&) const = default;

private:
    SeriesKey series_;
    std::vector<Sample> samples_;
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_SEGMENT_BLOCK_HPP