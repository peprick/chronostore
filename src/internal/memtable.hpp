#ifndef CHRONOSTORE_INTERNAL_MEMTABLE_HPP
#define CHRONOSTORE_INTERNAL_MEMTABLE_HPP

#include <chronostore/model.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace chronostore::internal {

class MemTable {
public:
    void put(SeriesKey series, Sample sample);

    [[nodiscard]] std::optional<Sample> latest(const SeriesKey& series) const;

    [[nodiscard]] std::vector<Sample> range(const SeriesKey& series, Timestamp start,
                                            Timestamp end) const;

    [[nodiscard]] std::size_t sample_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    using SamplesByTimestamp = std::map<Timestamp, double>;

    std::map<SeriesKey, SamplesByTimestamp> series_;
    std::size_t sample_count_{0};
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_MEMTABLE_HPP