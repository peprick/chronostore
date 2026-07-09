#include "internal/database_lock.hpp"

#include <chronostore/database.hpp>

#include <cerrno>
#include <memory>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace chronostore::internal {

class DatabaseLock::Impl {
public:
    explicit Impl(const std::filesystem::path& path) {
#ifdef _WIN32
        handle_ = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                    "failed to open database lock file");
        }

        OVERLAPPED overlapped{};

        if (::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                         &overlapped) == 0) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;

            if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) {
                throw DatabaseBusyError("database directory is already open");
            }

            throw std::system_error(static_cast<int>(error), std::system_category(),
                                    "failed to lock database directory");
        }
#else
        int flags = O_RDWR | O_CREAT;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        descriptor_ = ::open(path.c_str(), flags, 0644);

        if (descriptor_ < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "failed to open database lock file");
        }

        if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            const int error = errno;
            ::close(descriptor_);
            descriptor_ = -1;

            if (error == EWOULDBLOCK || error == EAGAIN) {
                throw DatabaseBusyError("database directory is already open");
            }

            throw std::system_error(error, std::generic_category(),
                                    "failed to lock database directory");
        }
#endif
    }

    ~Impl() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED overlapped{};
            static_cast<void>(::UnlockFileEx(handle_, 0, 1, 0, &overlapped));
            static_cast<void>(::CloseHandle(handle_));
        }
#else
        if (descriptor_ >= 0) {
            static_cast<void>(::flock(descriptor_, LOCK_UN));
            static_cast<void>(::close(descriptor_));
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int descriptor_{-1};
#endif
};

DatabaseLock::DatabaseLock(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {}

DatabaseLock::~DatabaseLock() = default;

} // namespace chronostore::internal
