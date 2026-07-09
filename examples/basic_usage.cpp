#include <chronostore/database.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

int main(int argument_count, char* arguments[]) {
    const std::filesystem::path database_path =
        argument_count > 1 ? std::filesystem::path(arguments[1])
                           : std::filesystem::path("chronostore-example-db");

    chronostore::Database database{database_path};
    const chronostore::SeriesKey series{"temperature", {chronostore::Tag{"room", "lab"}}};
    database.put(series, chronostore::Sample{chronostore::Timestamp{100}, 21.5});
    database.put(series, chronostore::Sample{chronostore::Timestamp{200}, 22.75});
    database.flush();

    const std::optional<chronostore::Sample> latest = database.latest(series);

    if (latest.has_value()) {
        std::cout << latest->timestamp().nanoseconds_since_epoch() << ' ' << latest->value()
                  << '\n';
    }

    return 0;
}
