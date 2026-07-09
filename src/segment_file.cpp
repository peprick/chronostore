#include "internal/segment_file.hpp"

#include "internal/byte_codec.hpp"
#include "internal/crc32c.hpp"
#include "internal/file_io.hpp"
#include "internal/segment_block_codec.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronostore::internal {
namespace {

constexpr std::uint32_t segment_file_magic = 0x46475343U;   // "CSGF"
constexpr std::uint32_t segment_footer_magic = 0x54465343U; // "CSFT"
constexpr std::uint32_t segment_index_magic = 0x58495343U;  // "CSIX"
constexpr std::uint16_t segment_file_version = 1U;
constexpr std::uint16_t segment_file_flags = 0U;
constexpr std::size_t segment_header_size = 32U;
constexpr std::size_t segment_footer_size = 32U;
constexpr std::size_t segment_index_header_size = 20U;
constexpr std::size_t checksum_size = sizeof(std::uint32_t);
constexpr std::size_t max_index_payload_size = 64U * 1024U * 1024U;
constexpr std::uint32_t max_segment_block_count = 1'000'000U;

struct SegmentEnvelope {
    std::uint32_t block_count;
    std::uint64_t index_offset;
    std::uint64_t index_size;
};

void append_bytes(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

void write_string(ByteWriter& writer, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("segment index string is too large");
    }

    writer.write_u32(static_cast<std::uint32_t>(value.size()));

    for (char character : value) {
        writer.write_u8(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }
}

bool read_string(ByteReader& reader, std::string& value) {
    std::uint32_t encoded_length = 0;

    if (!reader.read_u32(encoded_length) || encoded_length > reader.remaining()) {
        return false;
    }

    std::string decoded;
    decoded.reserve(encoded_length);

    for (std::uint32_t index = 0; index < encoded_length; ++index) {
        std::uint8_t byte = 0;

        if (!reader.read_u8(byte)) {
            return false;
        }

        decoded.push_back(std::bit_cast<char>(byte));
    }

    value = std::move(decoded);
    return true;
}

void write_series_key(ByteWriter& writer, const SeriesKey& series) {
    write_string(writer, series.measurement());

    if (series.tags().size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("segment index contains too many tags");
    }

    writer.write_u32(static_cast<std::uint32_t>(series.tags().size()));

    for (const Tag& tag : series.tags()) {
        write_string(writer, tag.key());
        write_string(writer, tag.value());
    }
}

std::optional<SeriesKey> read_series_key(ByteReader& reader) {
    try {
        std::string measurement;

        if (!read_string(reader, measurement)) {
            return std::nullopt;
        }

        std::uint32_t tag_count = 0;

        if (!reader.read_u32(tag_count)) {
            return std::nullopt;
        }

        if (static_cast<std::size_t>(tag_count) > reader.remaining() / 8U) {
            return std::nullopt;
        }

        std::vector<Tag> tags;

        for (std::uint32_t index = 0; index < tag_count; ++index) {
            std::string key;
            std::string value;

            if (!read_string(reader, key) || !read_string(reader, value)) {
                return std::nullopt;
            }

            if (!tags.empty() && key <= tags.back().key()) {
                return std::nullopt;
            }

            tags.emplace_back(std::move(key), std::move(value));
        }

        return SeriesKey{std::move(measurement), std::move(tags)};
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

std::vector<std::uint8_t> encode_header(std::uint32_t block_count, std::uint64_t index_offset,
                                        std::uint64_t index_size) {
    ByteWriter writer;
    writer.write_u32(segment_file_magic);
    writer.write_u16(segment_file_version);
    writer.write_u16(segment_file_flags);
    writer.write_u32(block_count);
    writer.write_u64(index_offset);
    writer.write_u64(index_size);
    writer.write_u32(crc32c(writer.data()));
    return writer.data();
}

std::vector<std::uint8_t> encode_footer(std::uint32_t block_count, std::uint64_t index_offset,
                                        std::uint64_t index_size) {
    ByteWriter writer;
    writer.write_u32(segment_footer_magic);
    writer.write_u16(segment_file_version);
    writer.write_u16(segment_file_flags);
    writer.write_u64(index_offset);
    writer.write_u64(index_size);
    writer.write_u32(block_count);
    writer.write_u32(crc32c(writer.data()));
    return writer.data();
}

SegmentEnvelope decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != segment_header_size) {
        throw SegmentFormatError("invalid segment header size");
    }

    ByteReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t block_count = 0;
    std::uint64_t index_offset = 0;
    std::uint64_t index_size = 0;
    std::uint32_t stored_checksum = 0;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(flags) ||
        !reader.read_u32(block_count) || !reader.read_u64(index_offset) ||
        !reader.read_u64(index_size) || !reader.read_u32(stored_checksum)) {
        throw SegmentFormatError("incomplete segment header");
    }

    if (magic != segment_file_magic) {
        throw SegmentFormatError("invalid segment header magic");
    }

    if (stored_checksum != crc32c(bytes.first(segment_header_size - checksum_size))) {
        throw SegmentFormatError("segment header checksum mismatch");
    }

    if (version != segment_file_version || flags != segment_file_flags) {
        throw SegmentFormatError("unsupported segment header version or flags");
    }

    if (block_count == 0U || block_count > max_segment_block_count) {
        throw SegmentFormatError("invalid segment block count");
    }

    return SegmentEnvelope{block_count, index_offset, index_size};
}

SegmentEnvelope decode_footer(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != segment_footer_size) {
        throw SegmentFormatError("invalid segment footer size");
    }

    ByteReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint64_t index_offset = 0;
    std::uint64_t index_size = 0;
    std::uint32_t block_count = 0;
    std::uint32_t stored_checksum = 0;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(flags) ||
        !reader.read_u64(index_offset) || !reader.read_u64(index_size) ||
        !reader.read_u32(block_count) || !reader.read_u32(stored_checksum)) {
        throw SegmentFormatError("incomplete segment footer");
    }

    if (magic != segment_footer_magic) {
        throw SegmentFormatError("invalid segment footer magic");
    }

    if (stored_checksum != crc32c(bytes.first(segment_footer_size - checksum_size))) {
        throw SegmentFormatError("segment footer checksum mismatch");
    }

    if (version != segment_file_version || flags != segment_file_flags) {
        throw SegmentFormatError("unsupported segment footer version or flags");
    }

    return SegmentEnvelope{block_count, index_offset, index_size};
}

std::vector<std::uint8_t> encode_index(const std::vector<SegmentIndexEntry>& entries) {
    ByteWriter payload;

    for (const SegmentIndexEntry& entry : entries) {
        payload.write_u64(entry.block_offset);
        payload.write_u32(entry.block_size);
        payload.write_u32(entry.sample_count);
        payload.write_u64(
            std::bit_cast<std::uint64_t>(entry.first_timestamp.nanoseconds_since_epoch()));
        payload.write_u64(
            std::bit_cast<std::uint64_t>(entry.last_timestamp.nanoseconds_since_epoch()));
        write_series_key(payload, entry.series);
    }

    if (payload.data().size() > max_index_payload_size) {
        throw std::length_error("segment index payload is too large");
    }

    const auto payload_size = static_cast<std::uint32_t>(payload.data().size());
    const std::uint32_t inverted_size = payload_size ^ std::numeric_limits<std::uint32_t>::max();

    ByteWriter writer;
    writer.write_u32(segment_index_magic);
    writer.write_u16(segment_file_version);
    writer.write_u16(segment_file_flags);
    writer.write_u32(static_cast<std::uint32_t>(entries.size()));
    writer.write_u32(payload_size);
    writer.write_u32(inverted_size);
    writer.write_bytes(payload.data());
    writer.write_u32(crc32c(writer.data()));
    return writer.data();
}

void validate_index(const std::vector<SegmentIndexEntry>& entries, std::uint64_t index_offset) {
    std::uint64_t expected_offset = segment_header_size;

    for (std::size_t index = 0; index < entries.size(); ++index) {
        const SegmentIndexEntry& entry = entries[index];

        if (entry.block_offset != expected_offset || entry.block_offset > index_offset ||
            entry.block_size == 0U || entry.sample_count == 0U ||
            entry.sample_count > max_samples_per_segment_block ||
            entry.last_timestamp < entry.first_timestamp) {
            throw SegmentFormatError("invalid segment index entry");
        }

        if (entry.block_size > index_offset - entry.block_offset) {
            throw SegmentFormatError("segment block extends into index");
        }

        expected_offset += entry.block_size;

        if (index != 0U) {
            const SegmentIndexEntry& previous = entries[index - 1U];

            if (entry.series < previous.series ||
                (entry.series == previous.series &&
                 entry.first_timestamp <= previous.last_timestamp)) {
                throw SegmentFormatError("segment index is not strictly ordered");
            }
        }
    }

    if (expected_offset != index_offset) {
        throw SegmentFormatError("segment block region has an invalid size");
    }
}

std::vector<SegmentIndexEntry> decode_index(std::span<const std::uint8_t> bytes,
                                            std::uint32_t expected_entry_count,
                                            std::uint64_t index_offset) {
    if (bytes.size() < segment_index_header_size + checksum_size) {
        throw SegmentFormatError("segment index is incomplete");
    }

    ByteReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t entry_count = 0;
    std::uint32_t payload_size = 0;
    std::uint32_t inverted_size = 0;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(flags) ||
        !reader.read_u32(entry_count) || !reader.read_u32(payload_size) ||
        !reader.read_u32(inverted_size)) {
        throw SegmentFormatError("segment index header is incomplete");
    }

    if (magic != segment_index_magic || version != segment_file_version ||
        flags != segment_file_flags) {
        throw SegmentFormatError("invalid or unsupported segment index header");
    }

    if (entry_count != expected_entry_count || entry_count == 0U ||
        entry_count > max_segment_block_count ||
        inverted_size != (payload_size ^ std::numeric_limits<std::uint32_t>::max()) ||
        payload_size > max_index_payload_size) {
        throw SegmentFormatError("invalid segment index metadata");
    }

    const std::size_t expected_size =
        segment_index_header_size + static_cast<std::size_t>(payload_size) + checksum_size;

    if (bytes.size() != expected_size) {
        throw SegmentFormatError("segment index size mismatch");
    }

    const std::size_t protected_size = bytes.size() - checksum_size;
    ByteReader checksum_reader(bytes.last(checksum_size));
    std::uint32_t stored_checksum = 0;

    if (!checksum_reader.read_u32(stored_checksum) ||
        stored_checksum != crc32c(bytes.first(protected_size))) {
        throw SegmentFormatError("segment index checksum mismatch");
    }

    ByteReader payload_reader(
        bytes.subspan(segment_index_header_size, static_cast<std::size_t>(payload_size)));
    std::vector<SegmentIndexEntry> entries;
    entries.reserve(entry_count);

    for (std::uint32_t index = 0; index < entry_count; ++index) {
        std::uint64_t block_offset = 0;
        std::uint32_t block_size = 0;
        std::uint32_t sample_count = 0;
        std::uint64_t first_timestamp_bits = 0;
        std::uint64_t last_timestamp_bits = 0;

        if (!payload_reader.read_u64(block_offset) || !payload_reader.read_u32(block_size) ||
            !payload_reader.read_u32(sample_count) ||
            !payload_reader.read_u64(first_timestamp_bits) ||
            !payload_reader.read_u64(last_timestamp_bits)) {
            throw SegmentFormatError("segment index entry is incomplete");
        }

        std::optional<SeriesKey> series = read_series_key(payload_reader);

        if (!series.has_value()) {
            throw SegmentFormatError("segment index contains an invalid series key");
        }

        entries.push_back(SegmentIndexEntry{
            std::move(series.value()), Timestamp{std::bit_cast<std::int64_t>(first_timestamp_bits)},
            Timestamp{std::bit_cast<std::int64_t>(last_timestamp_bits)}, block_offset, block_size,
            sample_count});
    }

    if (payload_reader.remaining() != 0U) {
        throw SegmentFormatError("segment index contains trailing payload bytes");
    }

    validate_index(entries, index_offset);
    return entries;
}

void validate_blocks(const std::vector<SegmentBlock>& blocks) {
    if (blocks.empty()) {
        throw std::invalid_argument("cannot write an empty segment");
    }

    if (blocks.size() > max_segment_block_count) {
        throw std::length_error("segment contains too many blocks");
    }

    for (std::size_t index = 1; index < blocks.size(); ++index) {
        const SegmentBlock& previous = blocks[index - 1U];
        const SegmentBlock& current = blocks[index];

        if (current.series() < previous.series() ||
            (current.series() == previous.series() &&
             current.first_timestamp() <= previous.last_timestamp())) {
            throw std::invalid_argument("segment blocks must be strictly ordered");
        }
    }
}

} // namespace

SegmentFileMetadata write_segment_file(const std::filesystem::path& path,
                                       const std::vector<SegmentBlock>& blocks) {
    validate_blocks(blocks);

    std::vector<std::uint8_t> file_bytes(segment_header_size, 0U);
    std::vector<SegmentIndexEntry> entries;
    entries.reserve(blocks.size());
    std::uint64_t sample_count = 0;

    for (const SegmentBlock& block : blocks) {
        const std::vector<std::uint8_t> encoded = encode_segment_block(block);

        if (encoded.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("encoded segment block is too large");
        }

        const std::uint64_t block_offset = file_bytes.size();
        const auto block_size = static_cast<std::uint32_t>(encoded.size());
        const auto block_sample_count = static_cast<std::uint32_t>(block.sample_count());

        entries.push_back(SegmentIndexEntry{block.series(), block.first_timestamp(),
                                            block.last_timestamp(), block_offset, block_size,
                                            block_sample_count});
        append_bytes(file_bytes, encoded);
        sample_count += block_sample_count;
    }

    const std::uint64_t index_offset = file_bytes.size();
    const std::vector<std::uint8_t> encoded_index = encode_index(entries);
    const std::uint64_t index_size = encoded_index.size();
    append_bytes(file_bytes, encoded_index);

    const std::vector<std::uint8_t> footer =
        encode_footer(static_cast<std::uint32_t>(blocks.size()), index_offset, index_size);
    append_bytes(file_bytes, footer);

    const std::vector<std::uint8_t> header =
        encode_header(static_cast<std::uint32_t>(blocks.size()), index_offset, index_size);

    if (header.size() != segment_header_size || footer.size() != segment_footer_size) {
        throw std::logic_error("segment envelope has an unexpected encoded size");
    }

    std::copy(header.begin(), header.end(), file_bytes.begin());
    atomic_write_file(path, file_bytes);

    return SegmentFileMetadata{static_cast<std::uint64_t>(file_bytes.size()), sample_count,
                               static_cast<std::uint32_t>(blocks.size())};
}

SegmentReader::SegmentReader(std::filesystem::path path) : path_(std::move(path)) {
    file_size_ = file_size_bytes(path_);

    if (file_size_ <
        segment_header_size + segment_footer_size + segment_index_header_size + checksum_size) {
        throw SegmentFormatError("segment file is too small");
    }

    const std::vector<std::uint8_t> header_bytes = read_file_bytes(path_, 0U, segment_header_size);
    const std::vector<std::uint8_t> footer_bytes =
        read_file_bytes(path_, file_size_ - segment_footer_size, segment_footer_size);
    const SegmentEnvelope header = decode_header(header_bytes);
    const SegmentEnvelope footer = decode_footer(footer_bytes);

    if (header.block_count != footer.block_count || header.index_offset != footer.index_offset ||
        header.index_size != footer.index_size) {
        throw SegmentFormatError("segment header and footer disagree");
    }

    if (header.index_offset < segment_header_size ||
        header.index_size < segment_index_header_size + checksum_size ||
        header.index_size > max_index_payload_size + segment_index_header_size + checksum_size ||
        header.index_offset > file_size_ || header.index_size > file_size_ - header.index_offset ||
        header.index_offset + header.index_size + segment_footer_size != file_size_) {
        throw SegmentFormatError("segment index bounds are invalid");
    }

    const std::vector<std::uint8_t> index_bytes =
        read_file_bytes(path_, header.index_offset, static_cast<std::size_t>(header.index_size));
    index_ = decode_index(index_bytes, header.block_count, header.index_offset);

    for (const SegmentIndexEntry& entry : index_) {
        sample_count_ += entry.sample_count;
    }
}

const std::filesystem::path& SegmentReader::path() const noexcept {
    return path_;
}

std::uint64_t SegmentReader::file_size() const noexcept {
    return file_size_;
}

std::uint64_t SegmentReader::sample_count() const noexcept {
    return sample_count_;
}

std::size_t SegmentReader::block_count() const noexcept {
    return index_.size();
}

const std::vector<SegmentIndexEntry>& SegmentReader::index() const noexcept {
    return index_;
}

SegmentBlock SegmentReader::read_block(const SegmentIndexEntry& entry) const {
    const std::vector<std::uint8_t> bytes =
        read_file_bytes(path_, entry.block_offset, entry.block_size);
    SegmentBlockDecodeResult decoded = decode_segment_block(bytes);

    if (!decoded.success() || decoded.bytes_consumed != bytes.size() ||
        decoded.block->series() != entry.series ||
        decoded.block->sample_count() != entry.sample_count ||
        decoded.block->first_timestamp() != entry.first_timestamp ||
        decoded.block->last_timestamp() != entry.last_timestamp) {
        throw SegmentFormatError("segment block does not match its index entry");
    }

    return std::move(decoded.block.value());
}

std::optional<Sample> SegmentReader::latest(const SeriesKey& series) const {
    const auto end = std::upper_bound(
        index_.begin(), index_.end(), series,
        [](const SeriesKey& key, const SegmentIndexEntry& entry) { return key < entry.series; });

    if (end == index_.begin()) {
        return std::nullopt;
    }

    const SegmentIndexEntry& entry = *(end - 1);

    if (entry.series != series) {
        return std::nullopt;
    }

    const SegmentBlock block = read_block(entry);
    return block.samples().back();
}

std::optional<Sample> SegmentReader::get(const SeriesKey& series, Timestamp timestamp) const {
    auto entry = std::lower_bound(index_.begin(), index_.end(), series,
                                  [](const SegmentIndexEntry& candidate, const SeriesKey& key) {
                                      return candidate.series < key;
                                  });

    for (; entry != index_.end() && entry->series == series; ++entry) {
        if (timestamp < entry->first_timestamp) {
            return std::nullopt;
        }

        if (timestamp > entry->last_timestamp) {
            continue;
        }

        const SegmentBlock block = read_block(*entry);
        const auto sample =
            std::lower_bound(block.samples().begin(), block.samples().end(), timestamp,
                             [](const Sample& candidate, Timestamp expected) {
                                 return candidate.timestamp() < expected;
                             });

        if (sample != block.samples().end() && sample->timestamp() == timestamp) {
            return *sample;
        }

        return std::nullopt;
    }

    return std::nullopt;
}

std::vector<Sample> SegmentReader::range(const SeriesKey& series, Timestamp start,
                                         Timestamp end) const {
    if (end < start) {
        throw std::invalid_argument("range end must not precede range start");
    }

    if (start == end) {
        return {};
    }

    auto entry = std::lower_bound(index_.begin(), index_.end(), series,
                                  [](const SegmentIndexEntry& candidate, const SeriesKey& key) {
                                      return candidate.series < key;
                                  });
    std::vector<Sample> result;

    for (; entry != index_.end() && entry->series == series; ++entry) {
        if (entry->last_timestamp < start) {
            continue;
        }

        if (entry->first_timestamp >= end) {
            break;
        }

        const SegmentBlock block = read_block(*entry);
        const std::vector<Sample>& samples = block.samples();
        const auto first = std::lower_bound(samples.begin(), samples.end(), start,
                                            [](const Sample& sample, Timestamp timestamp) {
                                                return sample.timestamp() < timestamp;
                                            });
        const auto last = std::lower_bound(first, samples.end(), end,
                                           [](const Sample& sample, Timestamp timestamp) {
                                               return sample.timestamp() < timestamp;
                                           });

        result.insert(result.end(), first, last);
    }

    return result;
}

std::vector<SegmentBlock> SegmentReader::blocks() const {
    std::vector<SegmentBlock> result;
    result.reserve(index_.size());

    for (const SegmentIndexEntry& entry : index_) {
        result.push_back(read_block(entry));
    }

    return result;
}

} // namespace chronostore::internal
