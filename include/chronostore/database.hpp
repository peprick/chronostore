#ifndef CHRONOSTORE_DATABASE_HPP
#define CHRONOSTORE_DATABASE_HPP

#include <chronostore/model.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace chronostore {

/// Controls when a successful write is synchronized to stable storage.
enum class Durability {
    /// Writes use the operating system's buffered I/O; call sync() or flush()
    /// to establish an explicit durability point.
    buffered,
    /// Every successful put() synchronizes its WAL record before returning.
    sync_on_write,
};

/// Thrown when another process owns the requested database directory.
class DatabaseBusyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Thrown when a persistent WAL, manifest, or segment fails validation.
class DatabaseCorruptionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Configuration applied when a Database is opened.
struct DatabaseOptions {
    Durability durability{Durability::sync_on_write};
    /// Number of in-memory logical samples that triggers a synchronous flush.
    /// A value of zero disables automatic flushes.
    std::size_t memtable_flush_threshold_samples{4096U};
};

/// A consistent snapshot of storage-engine counters.
struct DatabaseStats {
    /// Unique logical (series, timestamp) pairs across all storage layers.
    std::size_t sample_count{0};
    /// Logical samples currently resident in the active MemTable.
    std::size_t memtable_sample_count{0};
    /// Immutable segment files referenced by the current manifest.
    std::size_t segment_count{0};
    /// Current active write-ahead-log size.
    std::uint64_t wal_size_bytes{0};
};

/// A move-only, thread-safe owner of one embedded ChronoStore database.
///
/// One process may own a database directory at a time. Concurrent calls on the
/// same live instance are safe. Operations on a moved-from instance throw
/// std::logic_error.
class Database {
public:
    /// Creates the directory when needed and acquires its process lock.
    explicit Database(std::filesystem::path directory, DatabaseOptions options = {});

    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    /// Inserts a sample. Repeating a series/timestamp pair replaces its value.
    void put(SeriesKey series, Sample sample);

    /// Synchronizes the active WAL without flushing the MemTable.
    void sync();
    /// Publishes the MemTable as a new immutable segment.
    void flush();
    /// Merges all current immutable segments synchronously.
    void compact();

    /// Returns the sample with the greatest timestamp, or std::nullopt.
    [[nodiscard]]
    std::optional<Sample> latest(const SeriesKey& series) const;

    /// Returns the sample at exactly timestamp, or std::nullopt.
    [[nodiscard]]
    std::optional<Sample> get(const SeriesKey& series, Timestamp timestamp) const;

    /// Returns ordered samples in [start, end). Reversed bounds are invalid.
    [[nodiscard]]
    std::vector<Sample> range(const SeriesKey& series, Timestamp start, Timestamp end) const;

    /// Returns each known series once in canonical order.
    [[nodiscard]]
    std::vector<SeriesKey> series() const;

    /// Returns the number of unique logical samples.
    [[nodiscard]]
    std::size_t sample_count() const;

    /// Returns true when sample_count() is zero.
    [[nodiscard]]
    bool empty() const;

    /// Returns all engine counters under one shared lock.
    [[nodiscard]]
    DatabaseStats stats() const;

private:
    class Impl;

    [[nodiscard]] Impl& implementation();
    [[nodiscard]] const Impl& implementation() const;

    std::unique_ptr<Impl> impl_;
};

} // namespace chronostore

#endif // CHRONOSTORE_DATABASE_HPP
