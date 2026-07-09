#ifndef CHRONOSTORE_INTERNAL_MANIFEST_HPP
#define CHRONOSTORE_INTERNAL_MANIFEST_HPP

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronostore::internal {

class ManifestFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ManifestState {
    std::uint64_t generation{0};
    std::uint64_t logical_sample_count{0};
    std::vector<std::string> segment_files;

    bool operator==(const ManifestState&) const = default;
};

[[nodiscard]] std::filesystem::path manifest_path(const std::filesystem::path& directory);
[[nodiscard]] ManifestState load_manifest(const std::filesystem::path& directory);
void store_manifest(const std::filesystem::path& directory, const ManifestState& state);

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_MANIFEST_HPP
