#include <chronostore/database.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {

std::size_t parse_count(std::string_view text) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0U) {
        throw std::invalid_argument("sample count must be a positive integer");
    }

    return value;
}

double seconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

} // namespace

int main(int argument_count, char* arguments[]) {
    try {
        const std::size_t sample_count = argument_count > 1 ? parse_count(arguments[1]) : 100'000U;
        const std::filesystem::path database_path =
            argument_count > 2
                ? std::filesystem::path(arguments[2])
                : std::filesystem::temp_directory_path() / "chronostore-benchmark-db";
        std::error_code ignored;
        std::filesystem::remove_all(database_path, ignored);

        chronostore::DatabaseOptions options;
        options.durability = chronostore::Durability::buffered;
        options.memtable_flush_threshold = 8192U;
        chronostore::Database database{database_path, options};
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

        std::filesystem::remove_all(database_path, ignored);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "chronostore_bench: " << error.what() << '\n';
        return 1;
    }
}
