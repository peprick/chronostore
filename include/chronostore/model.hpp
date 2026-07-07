#ifndef CHRONOSTORE_MODEL_HPP
#define CHRONOSTORE_MODEL_HPP

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace chronostore {

class Tag {
public:
    Tag(std::string key, std::string value);

    [[nodiscard]] const std::string& key() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;

    bool operator==(const Tag&) const = default;
    auto operator<=>(const Tag&) const = default;

private:
    std::string key_;
    std::string value_;
};

class SeriesKey {
public:
    explicit SeriesKey(std::string measurement, std::vector<Tag> tags = {});

    [[nodiscard]] const std::string& measurement() const noexcept;
    [[nodiscard]] const std::vector<Tag>& tags() const noexcept;

    bool operator==(const SeriesKey&) const = default;
    auto operator<=>(const SeriesKey&) const = default;

private:
    std::string measurement_;
    std::vector<Tag> tags_;
};

class Timestamp {
public:
    explicit constexpr Timestamp(std::int64_t nanoseconds_since_epoch) noexcept
        : nanoseconds_since_epoch_(nanoseconds_since_epoch) {}

    [[nodiscard]] constexpr std::int64_t nanoseconds_since_epoch() const noexcept {
        return nanoseconds_since_epoch_;
    }

    bool operator==(const Timestamp&) const = default;
    auto operator<=>(const Timestamp&) const = default;

private:
    std::int64_t nanoseconds_since_epoch_;
};

class Sample {
public:
    Sample(Timestamp timestamp, double value);

    [[nodiscard]] Timestamp timestamp() const noexcept;
    [[nodiscard]] double value() const noexcept;

    bool operator==(const Sample&) const = default;

private:
    Timestamp timestamp_;
    double value_;
};

} // namespace chronostore

#endif // CHRONOSTORE_MODEL_HPP
