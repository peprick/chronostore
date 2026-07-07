#include <chronostore/model.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace chronostore {

Tag::Tag(std::string key, std::string value) : key_(std::move(key)), value_(std::move(value)) {
    if (key_.empty()) {
        throw std::invalid_argument("tag key cannot be empty");
    }
}

const std::string& Tag::key() const noexcept {
    return key_;
}

const std::string& Tag::value() const noexcept {
    return value_;
}

SeriesKey::SeriesKey(std::string measurement, std::vector<Tag> tags)
    : measurement_(std::move(measurement)), tags_(std::move(tags)) {
    if (measurement_.empty()) {
        throw std::invalid_argument("measurement cannot be empty");
    }
    // Canonical order makes series identity independent of input tag order.
    std::sort(tags_.begin(), tags_.end());

    const auto duplicate =
        std::adjacent_find(tags_.begin(), tags_.end(), [](const Tag& left, const Tag& right) {
            return left.key() == right.key();
        });

    if (duplicate != tags_.end()) {
        throw std::invalid_argument("duplicate tag key: " + duplicate->key());
    }
}

const std::string& SeriesKey::measurement() const noexcept {
    return measurement_;
}

const std::vector<Tag>& SeriesKey::tags() const noexcept {
    return tags_;
}

Sample::Sample(Timestamp timestamp, double value) : timestamp_(timestamp), value_(value) {
    if (!std::isfinite(value_)) {
        throw std::invalid_argument("sample value must be finite");
    }
}

Timestamp Sample::timestamp() const noexcept {
    return timestamp_;
}

double Sample::value() const noexcept {
    return value_;
}

} // namespace chronostore
