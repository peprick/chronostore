#ifndef CHRONOSTORE_MODEL_HPP
#define CHRONOSTORE_MODEL_HPP

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace chronostore {

/// A key/value dimension attached to a time series.
class Tag {
public:
    /// Constructs a tag. Keys must be non-empty; values may be empty.
    Tag(std::string key, std::string value);

    [[nodiscard]] const std::string& key() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;

    bool operator==(const Tag&) const = default;
    auto operator<=>(const Tag&) const = default;

private:
    std::string key_;
    std::string value_;
};

/// The canonical identity of a time series.
///
/// Tags are sorted by key during construction and duplicate keys are rejected,
/// so input tag order does not affect series identity.
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

/// A signed Unix timestamp with nanosecond precision.
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

/// A timestamped finite IEEE 754 double-precision value.
class Sample {
public:
    /// Throws std::invalid_argument when value is NaN or infinite.
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
