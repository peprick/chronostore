#include <internal/memtable.hpp>

#include <stdexcept>
#include <utility>

namespace chronostore::internal {

void MemTable::put(SeriesKey series, Sample sample) {
    auto series_it = series_.find(series);

    if (series_it == series_.end()) {
        auto insertion = series_.emplace(std::move(series), SamplesByTimestamp{});

        series_it = insertion.first;
    }

    SamplesByTimestamp& samples = series_it->second;

    const Timestamp timestamp = sample.timestamp();
    const double value = sample.value();

    auto sample_it = samples.find(timestamp);

    if (sample_it == samples.end()) {
        samples.emplace(timestamp, value);
        ++sample_count_;
        return;
    }

    sample_it->second = value;
}

std::optional<Sample> MemTable::latest(const SeriesKey& series) const {
    auto series_it = series_.find(series);

    if (series_it == series_.end()) {
        return std::nullopt;
    }

    const SamplesByTimestamp& samples = series_it->second;

    if (samples.empty()) {
        return std::nullopt;
    }

    auto sample_it = samples.end();
    --sample_it;

    return Sample(sample_it->first, sample_it->second);
}

std::optional<Sample> MemTable::get(const SeriesKey& series, Timestamp timestamp) const {
    const auto series_it = series_.find(series);

    if (series_it == series_.end()) {
        return std::nullopt;
    }

    const auto sample_it = series_it->second.find(timestamp);

    if (sample_it == series_it->second.end()) {
        return std::nullopt;
    }

    return Sample{sample_it->first, sample_it->second};
}

std::vector<Sample> MemTable::range(const SeriesKey& series, Timestamp start, Timestamp end) const {
    if (end < start) {
        throw std::invalid_argument("range end cannot be before start");
    }

    std::vector<Sample> result;

    auto series_it = series_.find(series);

    if (series_it == series_.end()) {
        return result;
    }

    const SamplesByTimestamp& samples = series_it->second;

    auto sample_it = samples.lower_bound(start);

    while (sample_it != samples.end()) {
        const Timestamp timestamp = sample_it->first;

        if (!(timestamp < end)) {
            break;
        }

        const double value = sample_it->second;

        result.push_back(Sample(timestamp, value));

        ++sample_it;
    }

    return result;
}

std::size_t MemTable::sample_count() const noexcept {
    return sample_count_;
}

bool MemTable::empty() const noexcept {
    return sample_count_ == 0;
}

std::vector<SeriesKey> MemTable::series() const {
    std::vector<SeriesKey> result;
    result.reserve(series_.size());

    for (const auto& entry : series_) {
        result.push_back(entry.first);
    }

    return result;
}

std::vector<MemTableSeriesSnapshot> MemTable::snapshot() const {
    std::vector<MemTableSeriesSnapshot> result;
    result.reserve(series_.size());

    for (const auto& [series, samples_by_timestamp] : series_) {
        std::vector<Sample> samples;
        samples.reserve(samples_by_timestamp.size());

        for (const auto& [timestamp, value] : samples_by_timestamp) {
            samples.emplace_back(timestamp, value);
        }

        result.push_back(MemTableSeriesSnapshot{series, std::move(samples)});
    }

    return result;
}

void MemTable::clear() noexcept {
    series_.clear();
    sample_count_ = 0;
}

} // namespace chronostore::internal
