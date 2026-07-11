#include <chronostore/database.hpp>

#include "internal/database_lock.hpp"
#include "internal/manifest.hpp"
#include "internal/segment_file.hpp"
#include "internal/storage_engine.hpp"
#include "internal/wal_recovery.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chronostore {
namespace {

internal::WalDurability to_internal_durability(Durability durability) {
    switch (durability) {
    case Durability::buffered:
        return internal::WalDurability::buffered;

    case Durability::sync_on_write:
        return internal::WalDurability::sync_on_append;
    }

    throw std::invalid_argument("unknown database durability mode");
}

template <typename Operation> decltype(auto) translate_corruption(Operation&& operation) {
    try {
        return std::forward<Operation>(operation)();
    } catch (const internal::ManifestFormatError& error) {
        throw DatabaseCorruptionError(error.what());
    } catch (const internal::SegmentFormatError& error) {
        throw DatabaseCorruptionError(error.what());
    } catch (const internal::WalFormatError& error) {
        throw DatabaseCorruptionError(error.what());
    }
}

std::filesystem::path prepare_database_directory(const std::filesystem::path& directory) {
    if (directory.empty()) {
        throw std::invalid_argument("database directory cannot be empty");
    }

    std::error_code error;

    const bool already_exists = std::filesystem::exists(directory, error);

    if (error) {
        throw std::system_error(error,
                                "failed to inspect database directory: " + directory.string());
    }

    if (!already_exists) {
        std::filesystem::create_directories(directory, error);

        if (error) {
            throw std::system_error(error,
                                    "failed to create database directory: " + directory.string());
        }
    }

    const bool is_directory = std::filesystem::is_directory(directory, error);

    if (error) {
        throw std::system_error(error, "failed to inspect database path: " + directory.string());
    }

    if (!is_directory) {
        throw std::invalid_argument("database path is not a directory: " + directory.string());
    }

    return directory / "chronostore.wal";
}

} // namespace

class Database::Impl {
public:
    Impl(const std::filesystem::path& directory, const std::filesystem::path& wal_path,
         DatabaseOptions options)
        : lock(directory / "LOCK"), engine(wal_path, to_internal_durability(options.durability)),
          flush_threshold_samples(options.memtable_flush_threshold_samples) {}

    internal::DatabaseLock lock;
    internal::StorageEngine engine;
    std::size_t flush_threshold_samples;
};

Database::Database(std::filesystem::path directory, DatabaseOptions options) : impl_(nullptr) {
    const std::filesystem::path wal_path = prepare_database_directory(directory);
    translate_corruption([&] { impl_ = std::make_unique<Impl>(directory, wal_path, options); });
}

Database::~Database() = default;

Database::Database(Database&&) noexcept = default;

Database& Database::operator=(Database&&) noexcept = default;

Database::Impl& Database::implementation() {
    if (impl_ == nullptr) {
        throw std::logic_error("database has been moved from");
    }

    return *impl_;
}

const Database::Impl& Database::implementation() const {
    if (impl_ == nullptr) {
        throw std::logic_error("database has been moved from");
    }

    return *impl_;
}

void Database::put(SeriesKey series, Sample sample) {
    Impl& impl = implementation();
    translate_corruption([&] { impl.engine.put(std::move(series), std::move(sample)); });

    if (impl.flush_threshold_samples != 0U &&
        impl.engine.memtable_sample_count() >= impl.flush_threshold_samples) {
        translate_corruption([&] { impl.engine.flush(); });
    }
}

void Database::sync() {
    implementation().engine.sync();
}

void Database::flush() {
    Impl& impl = implementation();
    translate_corruption([&] { impl.engine.flush(); });
}

void Database::compact() {
    Impl& impl = implementation();
    translate_corruption([&] { impl.engine.compact(); });
}

std::optional<Sample> Database::latest(const SeriesKey& series) const {
    const Impl& impl = implementation();
    return translate_corruption([&] { return impl.engine.latest(series); });
}

std::optional<Sample> Database::get(const SeriesKey& series, Timestamp timestamp) const {
    const Impl& impl = implementation();
    return translate_corruption([&] { return impl.engine.get(series, timestamp); });
}

std::vector<Sample> Database::range(const SeriesKey& series, Timestamp start, Timestamp end) const {
    const Impl& impl = implementation();
    return translate_corruption([&] { return impl.engine.range(series, start, end); });
}

std::vector<SeriesKey> Database::series() const {
    return implementation().engine.series();
}

std::size_t Database::sample_count() const {
    return implementation().engine.sample_count();
}

bool Database::empty() const {
    return implementation().engine.empty();
}

DatabaseStats Database::stats() const {
    const internal::StorageEngineStats stats = implementation().engine.stats();
    return DatabaseStats{stats.sample_count, stats.memtable_sample_count, stats.segment_count,
                         stats.wal_size_bytes};
}

} // namespace chronostore
