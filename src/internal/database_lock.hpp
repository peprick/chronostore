#ifndef CHRONOSTORE_INTERNAL_DATABASE_LOCK_HPP
#define CHRONOSTORE_INTERNAL_DATABASE_LOCK_HPP

#include <filesystem>
#include <memory>

namespace chronostore::internal {

class DatabaseLock {
public:
    explicit DatabaseLock(const std::filesystem::path& path);
    ~DatabaseLock();

    DatabaseLock(const DatabaseLock&) = delete;
    DatabaseLock& operator=(const DatabaseLock&) = delete;
    DatabaseLock(DatabaseLock&&) = delete;
    DatabaseLock& operator=(DatabaseLock&&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronostore::internal

#endif // CHRONOSTORE_INTERNAL_DATABASE_LOCK_HPP
