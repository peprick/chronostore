#ifndef CHRONOSTORE_INTERNAL_BYTE_CODEC_HPP
#define CHRONOSTORE_INTERNAL_BYTE_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronostore::internal {

class ByteWriter {
public:
    void write_u8(std::uint8_t value);
    void write_u16(std::uint16_t value);
    void write_u32(std::uint32_t value);
    void write_u64(std::uint64_t value);

    void write_bytes(const std::vector<std::uint8_t>& bytes);

    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept;

private:
    void write_unsigned(std::uint64_t value, std::size_t byte_count);

    std::vector<std::uint8_t> data_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept;
    bool read_u8(std::uint8_t& value);
    bool read_u16(std::uint16_t& value);
    bool read_u32(std::uint32_t& value);
    bool read_u64(std::uint64_t& value);

    bool read_bytes(std::size_t count, std::vector<std::uint8_t>& output);

    [[nodiscard]] std::size_t position() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;

private:
    bool read_unsigned(std::size_t byte_count, std::uint64_t& value);
    [[nodiscard]] bool can_read(std::size_t count) const noexcept;

    std::span<const std::uint8_t> data_;
    std::size_t position_{0};
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_BYTE_CODEC_HPP
