#include "internal/wal_file.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace chronostore::internal {
namespace {

constexpr int invalid_file_descriptor = -1;

[[noreturn]] void throw_file_error(const char* operation, const std::filesystem::path& path) {
    const int error_number = errno;

    const std::string message = std::string(operation) + ": " + path.string();

    throw std::system_error(error_number, std::generic_category(), message);
}

int open_for_append(const std::filesystem::path& path) {
#if defined(_WIN32)
    const int flags = _O_BINARY | _O_WRONLY | _O_CREAT | _O_APPEND | _O_NOINHERIT;

    const int permissions = _S_IREAD | _S_IWRITE;

    return ::_wopen(path.c_str(), flags, permissions);
#else
    int flags = O_WRONLY | O_CREAT | O_APPEND;

#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif

    return ::open(path.c_str(), flags, 0644);
#endif
}

int close_file(int file_descriptor) noexcept {
#if defined(_WIN32)
    return ::_close(file_descriptor);
#else
    return ::close(file_descriptor);
#endif
}

std::uint64_t find_file_end(int file_descriptor, const std::filesystem::path& path) {
#if defined(_WIN32)
    const __int64 end_position = ::_lseeki64(file_descriptor, 0, SEEK_END);
#else
    const off_t end_position = ::lseek(file_descriptor, 0, SEEK_END);
#endif

    if (end_position < 0) {
        throw_file_error("failed to inspect WAL", path);
    }

    return static_cast<std::uint64_t>(end_position);
}

void write_all(int file_descriptor, std::span<const std::uint8_t> bytes,
               const std::filesystem::path& path) {
    std::size_t total_written = 0;

    while (total_written < bytes.size()) {
        const std::size_t remaining = bytes.size() - total_written;

#if defined(_WIN32)
        const std::size_t maximum_request = std::numeric_limits<unsigned int>::max();

        const std::size_t request_size = remaining > maximum_request ? maximum_request : remaining;

        const int written = ::_write(file_descriptor, bytes.data() + total_written,
                                     static_cast<unsigned int>(request_size));
#else
        const ssize_t written = ::write(file_descriptor, bytes.data() + total_written, remaining);
#endif

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            throw_file_error("failed to append WAL", path);
        }

        if (written == 0) {
            throw std::runtime_error("zero-byte write while appending WAL: " + path.string());
        }

        total_written += static_cast<std::size_t>(written);
    }
}

int synchronize_file(int file_descriptor) {
#if defined(_WIN32)
    int result = 0;

    do {
        result = ::_commit(file_descriptor);
    } while (result < 0 && errno == EINTR);

    return result;
#else

#if defined(__APPLE__) && defined(F_FULLFSYNC)
    {
        int result = 0;

        do {
            result = ::fcntl(file_descriptor, F_FULLFSYNC);
        } while (result < 0 && errno == EINTR);

        if (result == 0) {
            return 0;
        }

        if (errno != EINVAL && errno != ENOTSUP) {
            return result;
        }
    }
#endif

    int result = 0;

    do {
        result = ::fsync(file_descriptor);
    } while (result < 0 && errno == EINTR);

    return result;
#endif
}

int truncate_file(int file_descriptor) {
#if defined(_WIN32)
    const errno_t result = ::_chsize_s(file_descriptor, 0);

    if (result != 0) {
        errno = static_cast<int>(result);
        return -1;
    }

    return 0;
#else
    int result = 0;

    do {
        result = ::ftruncate(file_descriptor, 0);
    } while (result < 0 && errno == EINTR);

    return result;
#endif
}

} // namespace

WalWriter::WalWriter(std::filesystem::path path, WalDurability durability)
    : path_(std::move(path)), durability_(durability) {
    file_descriptor_ = open_for_append(path_);

    if (file_descriptor_ == invalid_file_descriptor) {
        throw_file_error("failed to open WAL", path_);
    }

    try {
        size_bytes_ = find_file_end(file_descriptor_, path_);
    } catch (...) {
        close();
        throw;
    }
}

WalWriter::~WalWriter() {
    close();
}

void WalWriter::append(const WalPutRecord& record) {
    ensure_usable();

    const std::vector<std::uint8_t> encoded = encode_wal_record(record);

    const auto encoded_size = static_cast<std::uint64_t>(encoded.size());

    if (encoded_size > std::numeric_limits<std::uint64_t>::max() - size_bytes_) {
        throw std::length_error("WAL size would overflow");
    }

    try {
        write_all(file_descriptor_, encoded, path_);

        size_bytes_ += encoded_size;

        if (durability_ == WalDurability::sync_on_append) {
            sync();
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void WalWriter::sync() {
    ensure_usable();

    if (synchronize_file(file_descriptor_) < 0) {
        failed_ = true;
        throw_file_error("failed to synchronize WAL", path_);
    }
}

void WalWriter::reset() {
    ensure_usable();

    if (truncate_file(file_descriptor_) < 0 || synchronize_file(file_descriptor_) < 0) {
        failed_ = true;
        throw_file_error("failed to reset WAL", path_);
    }

    size_bytes_ = 0;
}

std::uint64_t WalWriter::size_bytes() const noexcept {
    return size_bytes_;
}

const std::filesystem::path& WalWriter::path() const noexcept {
    return path_;
}

void WalWriter::ensure_usable() const {
    if (file_descriptor_ == invalid_file_descriptor) {
        throw std::logic_error("WAL writer is closed");
    }

    if (failed_) {
        throw std::runtime_error("WAL writer is in a failed state: " + path_.string());
    }
}

void WalWriter::close() noexcept {
    if (file_descriptor_ == invalid_file_descriptor) {
        return;
    }

    static_cast<void>(close_file(file_descriptor_));

    file_descriptor_ = invalid_file_descriptor;
}

} // namespace chronostore::internal
