#include <chronostore/database.hpp>
#include <chronostore/version.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

int main(int argument_count, char* arguments[]) {
    if (argument_count != 2) {
        std::cerr << "usage: chronostore_package_consumer <database-path>\n";
        return 1;
    }

    const std::filesystem::path database_path{arguments[1]};
    const chronostore::SeriesKey series{"latency", {chronostore::Tag{"service", "orders"}}};
    const chronostore::Sample expected{chronostore::Timestamp{1'000'000'000}, 42.5};

    {
        chronostore::Database database{database_path};
        database.put(series, expected);
        database.flush();
    }

    const chronostore::Database reopened{database_path};
    const std::optional<chronostore::Sample> actual = reopened.get(series, expected.timestamp());

    if (chronostore::version_string != std::string_view{"0.1.0"} || actual != expected ||
        reopened.series() != std::vector<chronostore::SeriesKey>{series}) {
        std::cerr << "installed ChronoStore package failed its consumer contract\n";
        return 1;
    }

    std::cout << "ChronoStore " << chronostore::version_string << " package consumer passed\n";
    return 0;
}
