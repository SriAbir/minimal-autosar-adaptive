#include <persistency/key_value_storage_backend.hpp>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <iostream>

#if defined(__unix__) || defined(__APPLE__)
  #include <unistd.h>     // fsync, close
  #include <fcntl.h>      // open
  #include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace {

// POSIX-only: fsync a file by path (open/FSYNC/close). No-op on Windows.
inline void fsync_file_by_path(const std::string& path) {
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)path;
#endif
}

// POSIX-only: fsync a directory to persist the rename. No-op on Windows.
inline void fsync_dir_by_path(const std::string& dir) {
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)dir;
#endif
}

// Minimal guard against path traversal in keys. Adjust as needed.
inline bool key_is_safe(const std::string& key) {
    return !key.empty()
        && key.find('/') == std::string::npos
        && key.find('\\') == std::string::npos
        && key.find("..") == std::string::npos;
}

} // namespace

namespace persistency {

KeyValueStorageBackend::KeyValueStorageBackend(const std::string& base_path, size_t quota)
: base_path_(base_path), quota_(quota) {
    fs::create_directories(base_path_);
}

ara::core::Result<void>
KeyValueStorageBackend::SetValue(const std::string& key, const std::string& value) noexcept {
    std::lock_guard<std::mutex> lock(mtx_);

    // Key safety check like in GetValue
    if (!key_is_safe(key)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }

    fs::create_directories(base_path_);

    fs::path final = fs::path(base_path_) / key;
    fs::path tmp   = final; tmp += ".tmp";

    // Quota check WITHOUT re-locking
    std::error_code ec;
    size_t current  = GetUsedSpaceNoLock_();
    size_t old_size = fs::exists(final, ec) ? static_cast<size_t>(fs::file_size(final, ec)) : 0;
    size_t new_size = current - old_size + value.size();
    if (new_size > quota_) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kQuotaExceeded);
    }

    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    ofs.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!ofs) return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    ofs.close();

    fs::rename(tmp, final, ec);
    if (ec) { std::error_code ec2; fs::remove(tmp, ec2); return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown); }
    return {};
}

ara::core::Result<std::string> KeyValueStorageBackend::GetValue(const std::string& key) const noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!key_is_safe(key)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }
    fs::path file = fs::path(base_path_) / key;
    std::ifstream ifs(file, std::ios::binary);
    if (!ifs) return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    std::string value((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return value;
}

ara::core::Result<std::vector<std::string>> KeyValueStorageBackend::GetAllKeys() const noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> keys;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(base_path_, ec)) {
        if (ec) break; // minimal handling
        if (entry.is_regular_file()) {
            keys.push_back(entry.path().filename().string());
        }
    }
    return keys;
}

ara::core::Result<bool> KeyValueStorageBackend::HasKey(const std::string& key) const noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!key_is_safe(key)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }
    fs::path file = fs::path(base_path_) / key;
    return fs::exists(file);
}

ara::core::Result<void> KeyValueStorageBackend::RemoveKey(const std::string& key) noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!key_is_safe(key)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }
    fs::path file = fs::path(base_path_) / key;
    std::error_code ec;
    bool ok = fs::remove(file, ec);
    if (!ok || ec) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    }
#if defined(__unix__) || defined(__APPLE__)
    fsync_dir_by_path(fs::path(base_path_).string());
#endif
    return {};
}

ara::core::Result<void> KeyValueStorageBackend::SyncToStorage() const noexcept {
    // Minimal backend: writes are synchronous; nothing to do.
    return {};
}

ara::core::Result<void> KeyValueStorageBackend::DiscardPendingChanges() const noexcept {
    // Minimal backend: no staged changes.
    return {};
}

size_t KeyValueStorageBackend::GetUsedSpaceNoLock_() const noexcept {
    size_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(base_path_, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec)) {
            total += static_cast<size_t>(entry.file_size(ec));
            if (ec) ec.clear();
        }
    }
    return total;
}

size_t KeyValueStorageBackend::GetUsedSpace() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return GetUsedSpaceNoLock_();
}

} // namespace platform::persistency
