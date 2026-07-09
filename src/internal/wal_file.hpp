#ifndef CHRONOSTORE_INTERNAL_WAL_FILE_HPP
#define CHRONOSTORE_INTERNAL_WAL_FILE_HPP

#include "internal/wal_record.hpp"

#include <cstdint>
#include <filesystem>

namespace chronostore::internal {

enum class WalDurability { buffered, sync_on_append };

class WalWriter {
public:
    explicit WalWriter(std::filesystem::path path,
                       WalDurability durability = WalDurability::sync_on_append);

    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    WalWriter(WalWriter&&) = delete;
    WalWriter& operator=(WalWriter&&) = delete;

    void append(const WalPutRecord& record);
    void sync();
    void reset();

    [[nodiscard]] std::uint64_t size_bytes() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    void ensure_usable() const;
    void close() noexcept;

    std::filesystem::path path_;
    int file_descriptor_{-1};
    WalDurability durability_;
    std::uint64_t size_bytes_{0};
    bool failed_{false};
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_WAL_FILE_HPP
