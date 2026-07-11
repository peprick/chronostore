#include <chronostore/database.hpp>
#include <chronostore/version.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void print_usage(std::ostream& output) {
    output << "ChronoStore " << chronostore::version_string << "\n\n"
           << "Usage:\n"
           << "  chronostore put <db> <measurement> <timestamp-ns> <value> [key=value ...]\n"
           << "  chronostore get <db> <measurement> <timestamp-ns> [key=value ...]\n"
           << "  chronostore latest <db> <measurement> [key=value ...]\n"
           << "  chronostore range <db> <measurement> <start-ns> <end-ns> [key=value ...]\n"
           << "  chronostore series <db>\n"
           << "  chronostore stats <db>\n"
           << "  chronostore sync <db>\n"
           << "  chronostore flush <db>\n"
           << "  chronostore compact <db>\n"
           << "  chronostore --help\n"
           << "  chronostore --version\n";
}

std::int64_t parse_timestamp(std::string_view text) {
    std::int64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid timestamp: " + std::string(text));
    }

    return value;
}

double parse_value(std::string_view text) {
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid sample value: " + std::string(text));
    }

    return value;
}

std::vector<chronostore::Tag> parse_tags(int argument_count, char* arguments[], int begin) {
    std::vector<chronostore::Tag> tags;

    for (int index = begin; index < argument_count; ++index) {
        const std::string_view encoded(arguments[index]);
        const std::size_t separator = encoded.find('=');

        if (separator == std::string_view::npos || separator == 0U) {
            throw std::invalid_argument("invalid tag; expected key=value: " + std::string(encoded));
        }

        tags.emplace_back(std::string(encoded.substr(0U, separator)),
                          std::string(encoded.substr(separator + 1U)));
    }

    return tags;
}

chronostore::SeriesKey make_series(std::string_view measurement, int argument_count,
                                   char* arguments[], int tag_begin) {
    return chronostore::SeriesKey{std::string(measurement),
                                  parse_tags(argument_count, arguments, tag_begin)};
}

void print_sample(const chronostore::Sample& sample) {
    std::cout << sample.timestamp().nanoseconds_since_epoch() << '\t' << std::setprecision(17)
              << sample.value() << '\n';
}

int run_put(int argument_count, char* arguments[]) {
    if (argument_count < 6) {
        throw std::invalid_argument("put requires a database, measurement, timestamp, and value");
    }

    chronostore::Database database{std::filesystem::path(arguments[2])};
    chronostore::SeriesKey series = make_series(arguments[3], argument_count, arguments, 6);
    const chronostore::Sample sample{chronostore::Timestamp{parse_timestamp(arguments[4])},
                                     parse_value(arguments[5])};
    database.put(std::move(series), sample);
    return 0;
}

int run_latest(int argument_count, char* arguments[]) {
    if (argument_count < 4) {
        throw std::invalid_argument("latest requires a database and measurement");
    }

    chronostore::Database database{std::filesystem::path(arguments[2])};
    const chronostore::SeriesKey series = make_series(arguments[3], argument_count, arguments, 4);
    const std::optional<chronostore::Sample> sample = database.latest(series);

    if (!sample.has_value()) {
        return 2;
    }

    print_sample(sample.value());
    return 0;
}

int run_get(int argument_count, char* arguments[]) {
    if (argument_count < 5) {
        throw std::invalid_argument("get requires a database, measurement, and timestamp");
    }

    const chronostore::Database database{std::filesystem::path(arguments[2])};
    const chronostore::SeriesKey series = make_series(arguments[3], argument_count, arguments, 5);
    const std::optional<chronostore::Sample> sample =
        database.get(series, chronostore::Timestamp{parse_timestamp(arguments[4])});

    if (!sample.has_value()) {
        return 2;
    }

    print_sample(sample.value());
    return 0;
}

int run_range(int argument_count, char* arguments[]) {
    if (argument_count < 6) {
        throw std::invalid_argument(
            "range requires a database, measurement, start timestamp, and end timestamp");
    }

    chronostore::Database database{std::filesystem::path(arguments[2])};
    const chronostore::SeriesKey series = make_series(arguments[3], argument_count, arguments, 6);
    const chronostore::Timestamp start{parse_timestamp(arguments[4])};
    const chronostore::Timestamp end{parse_timestamp(arguments[5])};

    for (const chronostore::Sample& sample : database.range(series, start, end)) {
        print_sample(sample);
    }

    return 0;
}

int run_stats(int argument_count, char* arguments[]) {
    if (argument_count != 3) {
        throw std::invalid_argument("stats requires exactly one database path");
    }

    const chronostore::Database database{std::filesystem::path(arguments[2])};
    const chronostore::DatabaseStats stats = database.stats();
    std::cout << "samples\t" << stats.sample_count << '\n'
              << "memtable_samples\t" << stats.memtable_sample_count << '\n'
              << "segments\t" << stats.segment_count << '\n'
              << "wal_bytes\t" << stats.wal_size_bytes << '\n';
    return 0;
}

int run_series(int argument_count, char* arguments[]) {
    if (argument_count != 3) {
        throw std::invalid_argument("series requires exactly one database path");
    }

    const chronostore::Database database{std::filesystem::path(arguments[2])};

    for (const chronostore::SeriesKey& series : database.series()) {
        std::cout << series.measurement();

        for (const chronostore::Tag& tag : series.tags()) {
            std::cout << '\t' << tag.key() << '=' << tag.value();
        }

        std::cout << '\n';
    }

    return 0;
}

int run_maintenance(std::string_view command, int argument_count, char* arguments[]) {
    if (argument_count != 3) {
        throw std::invalid_argument(std::string(command) + " requires exactly one database path");
    }

    chronostore::Database database{std::filesystem::path(arguments[2])};

    if (command == "sync") {
        database.sync();
    } else if (command == "flush") {
        database.flush();
    } else {
        database.compact();
    }

    return 0;
}

} // namespace

int main(int argument_count, char* arguments[]) {
    try {
        if (argument_count < 2) {
            print_usage(std::cerr);
            return 1;
        }

        const std::string_view command(arguments[1]);

        if (command == "--help" || command == "help") {
            print_usage(std::cout);
            return 0;
        }

        if (command == "--version") {
            std::cout << "chronostore " << chronostore::version_string << '\n';
            return 0;
        }

        if (command == "put") {
            return run_put(argument_count, arguments);
        }

        if (command == "latest") {
            return run_latest(argument_count, arguments);
        }

        if (command == "get") {
            return run_get(argument_count, arguments);
        }

        if (command == "range") {
            return run_range(argument_count, arguments);
        }

        if (command == "stats") {
            return run_stats(argument_count, arguments);
        }

        if (command == "series") {
            return run_series(argument_count, arguments);
        }

        if (command == "sync" || command == "flush" || command == "compact") {
            return run_maintenance(command, argument_count, arguments);
        }

        std::cerr << "chronostore: unknown command: " << command << "\n\n";
        print_usage(std::cerr);
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "chronostore: " << error.what() << '\n';
        return 1;
    }
}
