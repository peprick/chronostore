#ifndef CHRONOSTORE_INTERNAL_WAL_RECOVERY_HPP
#define CHRONOSTORE_INTERNAL_WAL_RECOVERY_HPP

#include "internal/wal_record.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>

namespace chronostore::internal {

using WalReplayCallback = std::function<void(const WalPutRecord&)>;

struct WalReplayResult {
    std::uint64_t records_replayed{0};
    std::uint64_t valid_bytes{0};
    std::uint64_t incomplete_tail_bytes{0};

    [[nodiscard]]
    bool had_incomplete_tail() const noexcept {
        return incomplete_tail_bytes != 0;
    }
};

class WalFormatError : public std::runtime_error {
public:
    WalFormatError(std::uint64_t offset, WalDecodeError decode_error);

    [[nodiscard]]
    std::uint64_t offset() const noexcept;

    [[nodiscard]]
    WalDecodeError decode_error() const noexcept;

private:
    std::uint64_t offset_;
    WalDecodeError decode_error_;
};

[[nodiscard]]
WalReplayResult replay_wal(const std::filesystem::path& path, const WalReplayCallback& replay);

void repair_wal_tail(const std::filesystem::path& path, const WalReplayResult& replay_result);

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_WAL_RECOVERY_HPP