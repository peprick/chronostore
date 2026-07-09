#include "internal/file_io.hpp"

#include <cerrno>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/fcntl.h>
#endif
#endif

namespace chronostore::internal {
namespace {

std::runtime_error file_error(const std::string& action, const std::filesystem::path& path) {
    return std::runtime_error(action + ": " + path.string());
}

#ifdef _WIN32

void sync_file(const std::filesystem::path& path) {
    const int descriptor = ::_wopen(path.c_str(), _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);

    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "failed to open file for synchronization");
    }

    const int sync_result = ::_commit(descriptor);
    const int sync_error = errno;
    const int close_result = ::_close(descriptor);
    const int close_error = errno;

    if (sync_result != 0) {
        throw std::system_error(sync_error, std::generic_category(), "failed to synchronize file");
    }

    if (close_result != 0) {
        throw std::system_error(close_error, std::generic_category(),
                                "failed to close synchronized file");
    }
}

void replace_file(const std::filesystem::path& temporary, const std::filesystem::path& final_path) {
    if (::MoveFileExW(temporary.c_str(), final_path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                "failed to publish atomic file");
    }
}

#else

int open_for_sync(const std::filesystem::path& path, int flags) {
    while (true) {
        const int descriptor = ::open(path.c_str(), flags);

        if (descriptor >= 0 || errno != EINTR) {
            return descriptor;
        }
    }
}

void close_descriptor(int descriptor) {
    while (::close(descriptor) != 0) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "failed to close file");
        }

        // On POSIX an EINTR close has an unspecified descriptor state. Do not retry it.
        return;
    }
}

void sync_descriptor(int descriptor) {
#ifdef __APPLE__
    if (::fcntl(descriptor, F_FULLFSYNC) == 0) {
        return;
    }

    if (errno != ENOTSUP && errno != EINVAL) {
        throw std::system_error(errno, std::generic_category(), "failed to synchronize file");
    }
#endif

    while (::fsync(descriptor) != 0) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "failed to synchronize file");
        }
    }
}

void sync_file(const std::filesystem::path& path) {
    const int descriptor = open_for_sync(path, O_RDWR);

    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "failed to open file for synchronization");
    }

    try {
        sync_descriptor(descriptor);
        close_descriptor(descriptor);
    } catch (...) {
        const int ignored = ::close(descriptor);
        static_cast<void>(ignored);
        throw;
    }
}

void replace_file(const std::filesystem::path& temporary, const std::filesystem::path& final_path) {
    std::error_code error;
    std::filesystem::rename(temporary, final_path, error);

    if (error) {
        throw std::system_error(error, "failed to publish atomic file");
    }

    std::filesystem::path parent = final_path.parent_path();

    if (parent.empty()) {
        parent = ".";
    }

    const int descriptor = open_for_sync(parent, O_RDONLY);

    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "failed to open parent directory for synchronization");
    }

    try {
        while (::fsync(descriptor) != 0) {
            if (errno != EINTR) {
                throw std::system_error(errno, std::generic_category(),
                                        "failed to synchronize parent directory");
            }
        }

        close_descriptor(descriptor);
    } catch (...) {
        const int ignored = ::close(descriptor);
        static_cast<void>(ignored);
        throw;
    }
}

#endif

} // namespace

std::uint64_t file_size_bytes(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);

    if (error) {
        throw std::system_error(error, "failed to inspect file size");
    }

    if (size > std::numeric_limits<std::uint64_t>::max()) {
        throw file_error("file is too large", path);
    }

    return static_cast<std::uint64_t>(size);
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path, std::uint64_t offset,
                                          std::size_t count) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw file_error("file offset is too large", path);
    }

    if (count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw file_error("file read is too large", path);
    }

    std::ifstream input(path, std::ios::binary);

    if (!input.is_open()) {
        throw file_error("failed to open file", path);
    }

    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

    if (!input) {
        throw file_error("failed to seek file", path);
    }

    std::vector<std::uint8_t> bytes(count);

    if (count != 0U) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));

        if (input.gcount() != static_cast<std::streamsize>(count)) {
            throw file_error("failed to read complete file range", path);
        }
    }

    return bytes;
}

void atomic_write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);

        if (!output.is_open()) {
            throw file_error("failed to open temporary file", temporary);
        }

        if (!bytes.empty()) {
            if (bytes.size() >
                static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
                throw file_error("atomic write is too large", temporary);
            }

            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }

        output.flush();
        output.close();

        if (!output) {
            throw file_error("failed to write temporary file", temporary);
        }
    }

    sync_file(temporary);
    replace_file(temporary, path);
}

} // namespace chronostore::internal
