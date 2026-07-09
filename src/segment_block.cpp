#include "internal/segment_block.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace chronostore::internal {

SegmentBlock::SegmentBlock(SeriesKey series, std::vector<Sample> samples)
    : series_(std::move(series)), samples_(std::move(samples)) {
    if (samples_.empty()) {
        throw std::invalid_argument("segment block cannot be empty");
    }

    if (samples_.size() > max_samples_per_segment_block) {
        throw std::length_error("segment block contains too many samples");
    }

    for (std::size_t index = 1; index < samples_.size(); ++index) {
        const Timestamp previous_timestamp = samples_[index - 1].timestamp();

        const Timestamp current_timestamp = samples_[index].timestamp();

        if (current_timestamp <= previous_timestamp) {
            throw std::invalid_argument("segment block timestamps must be "
                                        "strictly increasing");
        }
    }
}

const SeriesKey& SegmentBlock::series() const noexcept {
    return series_;
}

const std::vector<Sample>& SegmentBlock::samples() const noexcept {
    return samples_;
}

std::size_t SegmentBlock::sample_count() const noexcept {
    return samples_.size();
}

Timestamp SegmentBlock::first_timestamp() const noexcept {
    return samples_.front().timestamp();
}

Timestamp SegmentBlock::last_timestamp() const noexcept {
    return samples_.back().timestamp();
}

} // namespace chronostore::internal