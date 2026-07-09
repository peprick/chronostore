#include "internal/manifest.hpp"

#include "internal/byte_codec.hpp"
#include "internal/crc32c.hpp"
#include "internal/file_io.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace chronostore::internal {
namespace {

constexpr std::uint32_t manifest_magic = 0x464D5343U; // "CSMF"
constexpr std::uint16_t manifest_version = 1U;
constexpr std::uint16_t manifest_flags = 0U;
constexpr std::size_t manifest_header_size = 36U;
constexpr std::size_t manifest_checksum_size = sizeof(std::uint32_t);
constexpr std::size_t max_manifest_payload_size = 16U * 1024U * 1024U;
constexpr std::uint32_t max_manifest_segments = 100'000U;

bool valid_segment_filename(const std::string& filename) {
    if (filename.empty() || filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        return false;
    }

    const std::filesystem::path path(filename);
    return !path.has_parent_path() && path.filename() == path && path.extension() == ".cst";
}

void validate_state(const ManifestState& state) {
    if (state.segment_files.size() > max_manifest_segments) {
        throw std::length_error("manifest contains too many segments");
    }

    std::set<std::string> unique_names;

    for (const std::string& filename : state.segment_files) {
        if (!valid_segment_filename(filename) || !unique_names.insert(filename).second) {
            throw std::invalid_argument("manifest contains an invalid segment filename");
        }
    }
}

void write_string(ByteWriter& writer, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("manifest filename is too long");
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

std::vector<std::uint8_t> encode_manifest(const ManifestState& state) {
    validate_state(state);
    ByteWriter payload;

    for (const std::string& filename : state.segment_files) {
        write_string(payload, filename);
    }

    if (payload.data().size() > max_manifest_payload_size) {
        throw std::length_error("manifest payload is too large");
    }

    const auto payload_size = static_cast<std::uint32_t>(payload.data().size());
    const std::uint32_t inverted_size = payload_size ^ std::numeric_limits<std::uint32_t>::max();

    ByteWriter writer;
    writer.write_u32(manifest_magic);
    writer.write_u16(manifest_version);
    writer.write_u16(manifest_flags);
    writer.write_u64(state.generation);
    writer.write_u64(state.logical_sample_count);
    writer.write_u32(static_cast<std::uint32_t>(state.segment_files.size()));
    writer.write_u32(payload_size);
    writer.write_u32(inverted_size);
    writer.write_bytes(payload.data());
    writer.write_u32(crc32c(writer.data()));
    return writer.data();
}

ManifestState decode_manifest(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < manifest_header_size + manifest_checksum_size) {
        throw ManifestFormatError("manifest is incomplete");
    }

    ByteReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint64_t generation = 0;
    std::uint64_t logical_sample_count = 0;
    std::uint32_t segment_count = 0;
    std::uint32_t payload_size = 0;
    std::uint32_t inverted_size = 0;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(flags) ||
        !reader.read_u64(generation) || !reader.read_u64(logical_sample_count) ||
        !reader.read_u32(segment_count) || !reader.read_u32(payload_size) ||
        !reader.read_u32(inverted_size)) {
        throw ManifestFormatError("manifest header is incomplete");
    }

    if (magic != manifest_magic) {
        throw ManifestFormatError("manifest magic is invalid");
    }

    if (version != manifest_version || flags != manifest_flags) {
        throw ManifestFormatError("manifest version or flags are unsupported");
    }

    if (segment_count > max_manifest_segments || payload_size > max_manifest_payload_size ||
        inverted_size != (payload_size ^ std::numeric_limits<std::uint32_t>::max())) {
        throw ManifestFormatError("manifest metadata is invalid");
    }

    const std::size_t expected_size =
        manifest_header_size + static_cast<std::size_t>(payload_size) + manifest_checksum_size;

    if (bytes.size() != expected_size) {
        throw ManifestFormatError("manifest size is inconsistent");
    }

    const std::size_t protected_size = bytes.size() - manifest_checksum_size;
    ByteReader checksum_reader(bytes.last(manifest_checksum_size));
    std::uint32_t stored_checksum = 0;

    if (!checksum_reader.read_u32(stored_checksum) ||
        stored_checksum != crc32c(bytes.first(protected_size))) {
        throw ManifestFormatError("manifest checksum mismatch");
    }

    ByteReader payload_reader(
        bytes.subspan(manifest_header_size, static_cast<std::size_t>(payload_size)));
    ManifestState state;
    state.generation = generation;
    state.logical_sample_count = logical_sample_count;
    state.segment_files.reserve(segment_count);

    for (std::uint32_t index = 0; index < segment_count; ++index) {
        std::string filename;

        if (!read_string(payload_reader, filename)) {
            throw ManifestFormatError("manifest filename is incomplete");
        }

        state.segment_files.push_back(std::move(filename));
    }

    if (payload_reader.remaining() != 0U) {
        throw ManifestFormatError("manifest contains trailing payload bytes");
    }

    try {
        validate_state(state);
    } catch (const std::exception& error) {
        throw ManifestFormatError(error.what());
    }

    return state;
}

} // namespace

std::filesystem::path manifest_path(const std::filesystem::path& directory) {
    return directory / "MANIFEST";
}

ManifestState load_manifest(const std::filesystem::path& directory) {
    const std::filesystem::path path = manifest_path(directory);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);

    if (error) {
        throw std::system_error(error, "failed to inspect manifest");
    }

    if (!exists) {
        return {};
    }

    const std::uint64_t size = file_size_bytes(path);

    if (size > max_manifest_payload_size + manifest_header_size + manifest_checksum_size) {
        throw ManifestFormatError("manifest file is too large");
    }

    return decode_manifest(read_file_bytes(path, 0U, static_cast<std::size_t>(size)));
}

void store_manifest(const std::filesystem::path& directory, const ManifestState& state) {
    const std::vector<std::uint8_t> encoded = encode_manifest(state);
    atomic_write_file(manifest_path(directory), encoded);
}

} // namespace chronostore::internal
