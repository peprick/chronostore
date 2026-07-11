#include "internal/storage_engine.hpp"

#include "internal/wal_recovery.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace chronostore::internal {
namespace {

std::filesystem::path database_directory_for(const std::filesystem::path& wal_path) {
    std::filesystem::path directory = wal_path.parent_path();
    return directory.empty() ? std::filesystem::path{"."} : directory;
}

bool contains_in_segments(const std::vector<SegmentReader>& segments, const SeriesKey& series,
                          Timestamp timestamp) {
    for (auto segment = segments.rbegin(); segment != segments.rend(); ++segment) {
        if (segment->get(series, timestamp).has_value()) {
            return true;
        }
    }

    return false;
}

std::vector<SegmentBlock>
blocks_from_snapshots(const std::vector<MemTableSeriesSnapshot>& snapshots) {
    std::vector<SegmentBlock> blocks;

    for (const MemTableSeriesSnapshot& snapshot : snapshots) {
        std::size_t begin = 0;

        while (begin < snapshot.samples.size()) {
            const std::size_t remaining = snapshot.samples.size() - begin;
            const std::size_t count = std::min(remaining, max_samples_per_segment_block);
            const auto first = snapshot.samples.begin() + static_cast<std::ptrdiff_t>(begin);
            const auto last = first + static_cast<std::ptrdiff_t>(count);
            blocks.emplace_back(snapshot.series, std::vector<Sample>{first, last});
            begin += count;
        }
    }

    return blocks;
}

} // namespace

StorageEngine::StorageEngine(const std::filesystem::path& wal_path, WalDurability durability)
    : StorageEngine(wal_path, durability, recover(wal_path)) {}

StorageEngine::StorageEngine(const std::filesystem::path& wal_path, WalDurability durability,
                             RecoveryState recovered)
    : database_directory_(database_directory_for(wal_path)),
      memtable_(std::move(recovered.memtable)), manifest_(std::move(recovered.manifest)),
      segments_(std::move(recovered.segments)), wal_writer_(wal_path, durability),
      logical_sample_count_(recovered.logical_sample_count) {
    if (wal_writer_.size_bytes() != recovered.wal_size) {
        throw std::runtime_error("WAL changed while opening storage engine: " + wal_path.string());
    }
}

StorageEngine::RecoveryState StorageEngine::recover(const std::filesystem::path& wal_path) {
    RecoveryState recovered;
    const std::filesystem::path directory = database_directory_for(wal_path);
    recovered.manifest = load_manifest(directory);
    recovered.logical_sample_count = recovered.manifest.logical_sample_count;
    recovered.segments.reserve(recovered.manifest.segment_files.size());
    std::uint64_t physical_segment_samples = 0;

    for (const std::string& filename : recovered.manifest.segment_files) {
        recovered.segments.emplace_back(directory / filename);
        physical_segment_samples += recovered.segments.back().sample_count();
    }

    const std::set<std::string> live_segments(recovered.manifest.segment_files.begin(),
                                              recovered.manifest.segment_files.end());
    std::error_code directory_error;

    for (std::filesystem::directory_iterator entry(directory, directory_error), end;
         !directory_error && entry != end; entry.increment(directory_error)) {
        const std::string filename = entry->path().filename().string();
        const bool orphan_segment = entry->path().extension() == ".cst" &&
                                    filename.starts_with("segment-") &&
                                    !live_segments.contains(filename);
        const bool stale_temporary =
            filename.starts_with("segment-") && entry->path().extension() == ".tmp";

        if (orphan_segment || stale_temporary) {
            std::error_code ignored;
            std::filesystem::remove(entry->path(), ignored);
        }
    }

    if (directory_error) {
        throw std::system_error(directory_error, "failed to inspect database directory");
    }

    if (recovered.logical_sample_count > physical_segment_samples ||
        (recovered.segments.empty() && recovered.logical_sample_count != 0U)) {
        throw ManifestFormatError("manifest logical sample count is inconsistent");
    }

    std::error_code existence_error;
    const bool wal_exists = std::filesystem::exists(wal_path, existence_error);

    if (existence_error) {
        throw std::system_error(existence_error, "failed to inspect WAL: " + wal_path.string());
    }

    if (!wal_exists) {
        WalWriter creator(wal_path, WalDurability::buffered);
        creator.sync();
        return recovered;
    }

    const WalReplayResult replay_result = replay_wal(wal_path, [&](const WalPutRecord& record) {
        const bool already_exists =
            recovered.memtable.get(record.series, record.sample.timestamp()).has_value() ||
            contains_in_segments(recovered.segments, record.series, record.sample.timestamp());
        recovered.memtable.put(record.series, record.sample);

        if (!already_exists) {
            ++recovered.logical_sample_count;
        }
    });

    repair_wal_tail(wal_path, replay_result);
    recovered.wal_size = replay_result.valid_bytes;
    return recovered;
}

bool StorageEngine::contains(const SeriesKey& series, Timestamp timestamp) const {
    return memtable_.get(series, timestamp).has_value() ||
           contains_in_segments(segments_, series, timestamp);
}

void StorageEngine::put(SeriesKey series, Sample sample) {
    std::unique_lock lock(mutex_);
    ensure_usable();
    const bool already_exists = contains(series, sample.timestamp());
    const WalPutRecord record{series, sample};

    try {
        wal_writer_.append(record);
        memtable_.put(std::move(series), sample);

        if (!already_exists) {
            ++logical_sample_count_;
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void StorageEngine::sync() {
    std::unique_lock lock(mutex_);
    ensure_usable();

    try {
        wal_writer_.sync();
    } catch (...) {
        failed_ = true;
        throw;
    }
}

std::vector<SegmentBlock> StorageEngine::make_memtable_blocks() const {
    return blocks_from_snapshots(memtable_.snapshot());
}

std::string StorageEngine::next_segment_filename() const {
    if (manifest_.generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("segment generation is exhausted");
    }

    std::ostringstream name;
    name << "segment-" << std::setfill('0') << std::setw(20) << (manifest_.generation + 1U)
         << ".cst";
    return name.str();
}

void StorageEngine::flush() {
    std::unique_lock lock(mutex_);
    ensure_usable();

    if (memtable_.empty()) {
        try {
            wal_writer_.sync();
        } catch (...) {
            failed_ = true;
            throw;
        }
        return;
    }

    try {
        const std::vector<SegmentBlock> blocks = make_memtable_blocks();
        const std::string filename = next_segment_filename();
        const std::filesystem::path path = database_directory_ / filename;
        static_cast<void>(write_segment_file(path, blocks));
        SegmentReader reader(path);

        ManifestState next = manifest_;
        ++next.generation;
        next.logical_sample_count = logical_sample_count_;
        next.segment_files.push_back(filename);
        segments_.reserve(segments_.size() + 1U);
        store_manifest(database_directory_, next);

        segments_.push_back(std::move(reader));
        manifest_ = std::move(next);
        wal_writer_.reset();
        memtable_.clear();
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void StorageEngine::compact() {
    std::unique_lock lock(mutex_);
    ensure_usable();

    if (segments_.size() <= 1U) {
        return;
    }

    try {
        using SamplesByTimestamp = std::map<Timestamp, double>;
        std::map<SeriesKey, SamplesByTimestamp> merged;

        for (const SegmentReader& segment : segments_) {
            for (const SegmentBlock& block : segment.blocks()) {
                SamplesByTimestamp& samples = merged[block.series()];

                for (const Sample& sample : block.samples()) {
                    samples[sample.timestamp()] = sample.value();
                }
            }
        }

        std::vector<MemTableSeriesSnapshot> snapshots;
        std::uint64_t compacted_sample_count = 0;

        for (const auto& [series, samples_by_timestamp] : merged) {
            std::vector<Sample> samples;
            samples.reserve(samples_by_timestamp.size());

            for (const auto& [timestamp, value] : samples_by_timestamp) {
                samples.emplace_back(timestamp, value);
            }

            compacted_sample_count += samples.size();
            snapshots.push_back(MemTableSeriesSnapshot{series, std::move(samples)});
        }

        if (compacted_sample_count != manifest_.logical_sample_count) {
            throw SegmentFormatError("compacted segment count disagrees with manifest");
        }

        const std::vector<SegmentBlock> blocks = blocks_from_snapshots(snapshots);
        const std::string filename = next_segment_filename();
        const std::filesystem::path path = database_directory_ / filename;
        static_cast<void>(write_segment_file(path, blocks));
        SegmentReader reader(path);
        std::vector<SegmentReader> replacement;
        replacement.reserve(1U);
        replacement.push_back(std::move(reader));

        ManifestState next;
        next.generation = manifest_.generation + 1U;
        next.logical_sample_count = manifest_.logical_sample_count;
        next.segment_files.push_back(filename);

        std::vector<std::filesystem::path> obsolete;
        obsolete.reserve(manifest_.segment_files.size());

        for (const std::string& old_filename : manifest_.segment_files) {
            obsolete.push_back(database_directory_ / old_filename);
        }

        store_manifest(database_directory_, next);
        segments_ = std::move(replacement);
        manifest_ = std::move(next);

        for (const std::filesystem::path& old_path : obsolete) {
            std::error_code ignored;
            std::filesystem::remove(old_path, ignored);
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
}

std::optional<Sample> StorageEngine::latest(const SeriesKey& series) const {
    std::shared_lock lock(mutex_);
    ensure_usable();
    std::optional<Sample> result;

    for (const SegmentReader& segment : segments_) {
        const std::optional<Sample> candidate = segment.latest(series);

        if (candidate.has_value() &&
            (!result.has_value() || candidate->timestamp() >= result->timestamp())) {
            result = candidate;
        }
    }

    const std::optional<Sample> memory_candidate = memtable_.latest(series);

    if (memory_candidate.has_value() &&
        (!result.has_value() || memory_candidate->timestamp() >= result->timestamp())) {
        result = memory_candidate;
    }

    return result;
}

std::optional<Sample> StorageEngine::get(const SeriesKey& series, Timestamp timestamp) const {
    std::shared_lock lock(mutex_);
    ensure_usable();

    const std::optional<Sample> memory_sample = memtable_.get(series, timestamp);

    if (memory_sample.has_value()) {
        return memory_sample;
    }

    for (auto segment = segments_.rbegin(); segment != segments_.rend(); ++segment) {
        const std::optional<Sample> persisted_sample = segment->get(series, timestamp);

        if (persisted_sample.has_value()) {
            return persisted_sample;
        }
    }

    return std::nullopt;
}

std::vector<Sample> StorageEngine::range(const SeriesKey& series, Timestamp start,
                                         Timestamp end) const {
    std::shared_lock lock(mutex_);
    ensure_usable();

    if (end < start) {
        throw std::invalid_argument("range end must not precede range start");
    }

    std::map<Timestamp, double> merged;

    for (const SegmentReader& segment : segments_) {
        for (const Sample& sample : segment.range(series, start, end)) {
            merged[sample.timestamp()] = sample.value();
        }
    }

    for (const Sample& sample : memtable_.range(series, start, end)) {
        merged[sample.timestamp()] = sample.value();
    }

    std::vector<Sample> result;
    result.reserve(merged.size());

    for (const auto& [timestamp, value] : merged) {
        result.emplace_back(timestamp, value);
    }

    return result;
}

std::vector<SeriesKey> StorageEngine::series() const {
    std::shared_lock lock(mutex_);
    ensure_usable();
    std::set<SeriesKey> unique_series;

    for (const SegmentReader& segment : segments_) {
        for (const SegmentIndexEntry& entry : segment.index()) {
            unique_series.insert(entry.series);
        }
    }

    for (const SeriesKey& series : memtable_.series()) {
        unique_series.insert(series);
    }

    return {unique_series.begin(), unique_series.end()};
}

std::size_t StorageEngine::sample_count() const {
    std::shared_lock lock(mutex_);
    ensure_usable();

    if (logical_sample_count_ > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("logical sample count exceeds size_t");
    }

    return static_cast<std::size_t>(logical_sample_count_);
}

std::size_t StorageEngine::memtable_sample_count() const {
    std::shared_lock lock(mutex_);
    ensure_usable();
    return memtable_.sample_count();
}

bool StorageEngine::empty() const {
    std::shared_lock lock(mutex_);
    ensure_usable();
    return logical_sample_count_ == 0U;
}

std::size_t StorageEngine::segment_count() const {
    std::shared_lock lock(mutex_);
    ensure_usable();
    return segments_.size();
}

std::uint64_t StorageEngine::wal_size_bytes() const {
    std::shared_lock lock(mutex_);
    ensure_usable();
    return wal_writer_.size_bytes();
}

StorageEngineStats StorageEngine::stats() const {
    std::shared_lock lock(mutex_);
    ensure_usable();

    if (logical_sample_count_ > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("logical sample count exceeds size_t");
    }

    return StorageEngineStats{static_cast<std::size_t>(logical_sample_count_),
                              memtable_.sample_count(), segments_.size(), wal_writer_.size_bytes()};
}

void StorageEngine::ensure_usable() const {
    if (failed_) {
        throw std::runtime_error("storage engine is in a failed state; restart is required");
    }
}

} // namespace chronostore::internal
