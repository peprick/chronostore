#ifndef CHRONOSTORE_INTERNAL_SEGMENT_BLOCK_CODEC_HPP
#define CHRONOSTORE_INTERNAL_SEGMENT_BLOCK_CODEC_HPP

#include "internal/segment_block.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace chronostore::internal {

enum class SegmentBlockDecodeError {
    none,
    incomplete_block,
    invalid_magic,
    unsupported_version,
    unsupported_flags,
    invalid_length,
    invalid_sample_count,
    checksum_mismatch,
    invalid_payload
};

struct SegmentBlockDecodeResult {
    std::optional<SegmentBlock> block;
    SegmentBlockDecodeError error{SegmentBlockDecodeError::none};
    std::size_t bytes_consumed{0};

    [[nodiscard]] bool success() const noexcept {
        return block.has_value() && error == SegmentBlockDecodeError::none;
    }
};

[[nodiscard]] std::vector<std::uint8_t> encode_segment_block(const SegmentBlock& block);

[[nodiscard]] SegmentBlockDecodeResult decode_segment_block(std::span<const std::uint8_t> bytes);

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_SEGMENT_BLOCK_CODEC_HPP
