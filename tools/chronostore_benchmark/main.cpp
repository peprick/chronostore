#include <chronostore/database.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

std::size_t parse_count(std::string_view text) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0U ||
        value > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("sample count must be a positive integer");
    }

    return value;
}

std::filesystem::path default_database_path() {
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("chronostore-benchmark-" + std::to_string(token));
}

class ScratchDatabaseDirectory {
public:
    explicit ScratchDatabaseDirectory(std::filesystem::path path) : path_(std::move(path)) {
        std::error_code error;
        const bool created = std::filesystem::create_directory(path_, error);

        if (error) {
            throw std::system_error(error, "failed to create benchmark database directory");
        }

        if (!created) {
            throw std::invalid_argument("benchmark database path already exists: " +
                                        path_.string());
        }
    }

    ~ScratchDatabaseDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScratchDatabaseDirectory(const ScratchDatabaseDirectory&) = delete;
    ScratchDatabaseDirectory& operator=(const ScratchDatabaseDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

double seconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

} // namespace

int main(int argument_count, char* arguments[]) {
    try {
        if (argument_count > 3) {
            throw std::invalid_argument(
                "usage: chronostore-benchmark [sample-count] [new-scratch-directory]");
        }

        const std::size_t sample_count = argument_count > 1 ? parse_count(arguments[1]) : 100'000U;
        ScratchDatabaseDirectory scratch{argument_count > 2 ? std::filesystem::path(arguments[2])
                                                            : default_database_path()};

        chronostore::DatabaseOptions options;
        options.durability = chronostore::Durability::buffered;
        options.memtable_flush_threshold_samples = 8192U;
        chronostore::Database database{scratch.path(), options};
        const chronostore::SeriesKey series{"benchmark",
                                            {chronostore::Tag{"source", "sequential"}}};

        const auto write_start = std::chrono::steady_clock::now();

        for (std::size_t index = 0; index < sample_count; ++index) {
            database.put(series, chronostore::Sample{
                                     chronostore::Timestamp{static_cast<std::int64_t>(index)},
                                     static_cast<double>(index) * 0.5});
        }

        database.flush();
        const auto write_end = std::chrono::steady_clock::now();
        constexpr std::size_t query_count = 1000U;
        std::size_t samples_read = 0;
        const auto query_start = std::chrono::steady_clock::now();

        for (std::size_t query = 0; query < query_count; ++query) {
            const std::size_t start_index = (query * 97U) % sample_count;
            const std::size_t available = sample_count - start_index;
            const std::size_t width = available < 100U ? available : 100U;
            samples_read +=
                database
                    .range(series, chronostore::Timestamp{static_cast<std::int64_t>(start_index)},
                           chronostore::Timestamp{static_cast<std::int64_t>(start_index + width)})
                    .size();
        }

        const auto query_end = std::chrono::steady_clock::now();
        const double write_seconds = seconds(write_end - write_start);
        const double query_seconds = seconds(query_end - query_start);
        const chronostore::DatabaseStats stats = database.stats();

        std::cout << std::fixed << std::setprecision(3) << "samples=" << sample_count << '\n'
                  << "write_seconds=" << write_seconds << '\n'
                  << "write_samples_per_second="
                  << static_cast<double>(sample_count) / write_seconds << '\n'
                  << "query_seconds=" << query_seconds << '\n'
                  << "queries_per_second=" << static_cast<double>(query_count) / query_seconds
                  << '\n'
                  << "samples_read=" << samples_read << '\n'
                  << "segments=" << stats.segment_count << '\n';

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "chronostore-benchmark: " << error.what() << '\n';
        return 1;
    }
}
