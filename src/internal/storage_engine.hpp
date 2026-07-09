#ifndef CHRONOSTORE_INTERNAL_STORAGE_ENGINE_HPP
#define CHRONOSTORE_INTERNAL_STORAGE_ENGINE_HPP

#include "internal/manifest.hpp"
#include "internal/memtable.hpp"
#include "internal/segment_file.hpp"
#include "internal/wal_file.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace chronostore::internal {

struct StorageEngineStats {
    std::size_t sample_count;
    std::size_t memory_sample_count;
    std::size_t segment_count;
    std::uint64_t wal_size_bytes;
};

class StorageEngine {
public:
    explicit StorageEngine(const std::filesystem::path& wal_path,
                           WalDurability durability = WalDurability::sync_on_append);

    ~StorageEngine() = default;

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    StorageEngine(StorageEngine&&) = delete;
    StorageEngine& operator=(StorageEngine&&) = delete;

    void put(SeriesKey series, Sample sample);
    void sync();
    void flush();
    void compact();

    [[nodiscard]] std::optional<Sample> latest(const SeriesKey& series) const;
    [[nodiscard]] std::optional<Sample> get(const SeriesKey& series, Timestamp timestamp) const;
    [[nodiscard]] std::vector<Sample> range(const SeriesKey& series, Timestamp start,
                                            Timestamp end) const;
    [[nodiscard]] std::size_t sample_count() const;
    [[nodiscard]] std::size_t memory_sample_count() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t segment_count() const;
    [[nodiscard]] std::uint64_t wal_size_bytes() const;
    [[nodiscard]] StorageEngineStats stats() const;

private:
    struct RecoveryState {
        MemTable memtable;
        ManifestState manifest;
        std::vector<SegmentReader> segments;
        std::uint64_t logical_sample_count{0};
        std::uint64_t wal_size{0};
    };

    StorageEngine(const std::filesystem::path& wal_path, WalDurability durability,
                  RecoveryState recovered);

    [[nodiscard]] static RecoveryState recover(const std::filesystem::path& wal_path);
    [[nodiscard]] bool contains(const SeriesKey& series, Timestamp timestamp) const;
    [[nodiscard]] std::vector<SegmentBlock> make_memtable_blocks() const;
    [[nodiscard]] std::string next_segment_filename() const;
    void ensure_usable() const;

    std::filesystem::path database_directory_;
    MemTable memtable_;
    ManifestState manifest_;
    std::vector<SegmentReader> segments_;
    WalWriter wal_writer_;
    std::uint64_t logical_sample_count_{0};
    bool failed_{false};
    mutable std::shared_mutex mutex_;
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_STORAGE_ENGINE_HPP
