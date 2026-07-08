#include "internal/byte_codec.hpp"

#include <utility>

namespace chronostore::internal {

void ByteWriter::write_unsigned(std::uint64_t value, std::size_t byte_count) {
    for (std::size_t index = 0; index < byte_count; ++index) {
        const auto byte = static_cast<std::uint8_t>(value & 0xFFU);

        data_.push_back(byte);
        value >>= 8U;
    }
}

void ByteWriter::write_u8(std::uint8_t value) {
    write_unsigned(value, 1);
}

void ByteWriter::write_u16(std::uint16_t value) {
    write_unsigned(value, 2);
}

void ByteWriter::write_u32(std::uint32_t value) {
    write_unsigned(value, 4);
}

void ByteWriter::write_u64(std::uint64_t value) {
    write_unsigned(value, 8);
}

void ByteWriter::write_bytes(const std::vector<std::uint8_t>& bytes) {
    for (std::uint8_t byte : bytes) {
        data_.push_back(byte);
    }
}

const std::vector<std::uint8_t>& ByteWriter::data() const noexcept {
    return data_;
}

ByteReader::ByteReader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

bool ByteReader::read_unsigned(std::size_t byte_count, std::uint64_t& value) {
    if (byte_count > sizeof(std::uint64_t)) {
        return false;
    }
    if (!can_read(byte_count)) {
        return false;
    }

    std::uint64_t decoded = 0;

    for (std::size_t index = 0; index < byte_count; ++index) {
        const std::uint64_t byte = data_[position_ + index];
        const std::size_t shift = index * 8U;

        decoded |= byte << shift;
    }

    position_ += byte_count;
    value = decoded;

    return true;
}

bool ByteReader::read_u8(std::uint8_t& value) {
    std::uint64_t decoded = 0;

    if (!read_unsigned(1, decoded)) {
        return false;
    }

    value = static_cast<std::uint8_t>(decoded);
    return true;
}

bool ByteReader::read_u16(std::uint16_t& value) {
    std::uint64_t decoded = 0;

    if (!read_unsigned(2, decoded)) {
        return false;
    }

    value = static_cast<std::uint16_t>(decoded);
    return true;
}

bool ByteReader::read_u32(std::uint32_t& value) {
    std::uint64_t decoded = 0;

    if (!read_unsigned(4, decoded)) {
        return false;
    }

    value = static_cast<std::uint32_t>(decoded);
    return true;
}

bool ByteReader::read_u64(std::uint64_t& value) {
    std::uint64_t decoded = 0;

    if (!read_unsigned(8, decoded)) {
        return false;
    }

    value = decoded;
    return true;
}

bool ByteReader::read_bytes(std::size_t count, std::vector<std::uint8_t>& output) {
    if (!can_read(count)) {
        return false;
    }

    std::vector<std::uint8_t> decoded;
    decoded.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        decoded.push_back(data_[position_ + index]);
    }

    position_ += count;
    output = std::move(decoded);

    return true;
}

std::size_t ByteReader::position() const noexcept {
    return position_;
}

std::size_t ByteReader::remaining() const noexcept {
    return data_.size() - position_;
}

bool ByteReader::can_read(std::size_t count) const noexcept {
    return count <= remaining();
}

} // namespace chronostore::internal
