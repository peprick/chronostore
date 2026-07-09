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

enum class Durability { buffered, sync_on_write };

class DatabaseBusyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DatabaseCorruptionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DatabaseOptions {
    Durability durability{Durability::sync_on_write};
    std::size_t memtable_flush_threshold{4096U};
};

struct DatabaseStats {
    std::size_t sample_count{0};
    std::size_t memory_sample_count{0};
    std::size_t segment_count{0};
    std::uint64_t wal_size_bytes{0};
};

class Database {
public:
    explicit Database(std::filesystem::path directory, DatabaseOptions options = {});

    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    void put(SeriesKey series, Sample sample);

    void sync();
    void flush();
    void compact();

    [[nodiscard]]
    std::optional<Sample> latest(const SeriesKey& series) const;

    [[nodiscard]]
    std::optional<Sample> get(const SeriesKey& series, Timestamp timestamp) const;

    [[nodiscard]]
    std::vector<Sample> range(const SeriesKey& series, Timestamp start, Timestamp end) const;

    [[nodiscard]]
    std::vector<SeriesKey> series() const;

    [[nodiscard]]
    std::size_t sample_count() const;

    [[nodiscard]]
    bool empty() const;

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
