#include "ara/per/file_storage.hpp"
#include <filesystem>
#include <fstream>
#include <system_error>
#include <persistency/storage_registry.hpp>


#if defined(__unix__) || defined(__APPLE__)
  #include <unistd.h>   // fsync, close
  #include <fcntl.h>    // open
  #include <sys/stat.h>
#endif

using persistency::StorageRegistry;
using persistency::StorageType;

namespace fs = std::filesystem;

namespace {
// Reject traversal / absolute paths inside the storage
inline bool rel_path_is_safe(std::string_view p) {
    if (p.empty()) return false;
    std::string s(p);
    return s.find("..") == std::string::npos &&
           s.find(':') == std::string::npos &&
           !fs::path(s).is_absolute();
}

inline void fsync_file_by_path(const std::string& path) {
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
#else
    (void)path;
#endif
}

inline void fsync_dir_by_path(const std::string& dir) {
#if defined(__unix__) || defined(__APPLE__)
    int flags = O_RDONLY;
  #ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
  #endif
    int fd = ::open(dir.c_str(), flags);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
#else
    (void)dir;
#endif
}

} // namespace

namespace ara::per {

FileStorage::FileStorage(const std::string& base_path, size_t quota)
    : base_path_(base_path), quota_(quota) {
    fs::create_directories(base_path_);
}

ara::core::Result<void>
FileStorage::WriteFile(std::string_view rel, const std::vector<uint8_t>& data) noexcept {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!rel_path_is_safe(rel)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }

    fs::path file = fs::path(base_path_) / std::string(rel);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec); // best effort

    // --- Quota enforcement ---
    size_t current = GetUsedSpace();                // O(n); OK for minimal impl
    size_t old_size = 0;
    if (fs::exists(file)) {
        std::error_code se;
        old_size = static_cast<size_t>(fs::file_size(file, se));
    }
    size_t new_size = current - old_size + data.size();
    if (new_size > quota_) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kQuotaExceeded);
    }

    // --- Atomic write: tmp → fsync → rename → fsync dir ---
    fs::path tmp = file;
    tmp += ".tmp";

    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!ofs) return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
        ofs.flush();
        ofs.close();
        fsync_file_by_path(tmp.string());
    }

    fs::rename(tmp, file, ec);
    if (ec) {
        std::error_code ec2; fs::remove(tmp, ec2);
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    }

#if defined(__unix__) || defined(__APPLE__)
    fsync_dir_by_path(file.parent_path().string());
#endif

    return {};
}

ara::core::Result<std::vector<uint8_t>>
FileStorage::ReadFile(std::string_view rel) const noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!rel_path_is_safe(rel)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }
    fs::path file = fs::path(base_path_) / std::string(rel);
    std::ifstream ifs(file, std::ios::binary);
    if (!ifs) return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
    return data;
}

ara::core::Result<void>
FileStorage::RemoveFile(std::string_view rel) noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!rel_path_is_safe(rel)) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kPermissionDenied);
    }
    fs::path file = fs::path(base_path_) / std::string(rel);
    std::error_code ec;
    bool ok = fs::remove(file, ec);
    if (!ok || ec) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    }
#if defined(__unix__) || defined(__APPLE__)
    fsync_dir_by_path(file.parent_path().string());
#endif
    return {};
}

ara::core::Result<std::vector<std::string>>
FileStorage::ListFiles() const noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(base_path_, ec)) {
        if (ec) break;
        if (entry.is_regular_file()) {
            files.push_back(entry.path().lexically_relative(base_path_).string());
        }
    }
    return files;
}

size_t FileStorage::GetUsedSpace() const {
    // Caller must hold lock if consistency vs concurrent writes matters
    size_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(base_path_, ec)) {
        if (ec) break;
        if (entry.is_regular_file()) {
            total += static_cast<size_t>(entry.file_size(ec));
            if (ec) { ec.clear(); }
        }
    }
    return total;
}

ara::core::Result<void> FileStorage::SyncToStorage() const noexcept {
    // Minimal backend: atomic writes already persisted; nothing more to do.
    return {};
}

ara::core::Result<void> FileStorage::DiscardPendingChanges() const noexcept {
    // Minimal backend: no staging.
    return {};
}

// If you have a registry, include it and enable this flag.
// #define USE_STORAGE_REGISTRY 1
#ifdef USE_STORAGE_REGISTRY
  #include "platform/persistency/storage_registry.hpp" // adjust path
#endif

ara::core::Result<SharedFileHandle>
OpenFileStorage(ara::core::InstanceSpecifier fs_spec, size_t quota_bytes) noexcept {


    if (!StorageRegistry::Instance().IsInitialized()) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    }

    auto cfg = StorageRegistry::Instance().Lookup(fs_spec.ToString());
    if (!cfg || cfg->type != StorageType::Files) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    }

    return std::make_shared<FileStorage>(cfg->base_path, cfg->quota_bytes);
}

ara::core::Result<void> RecoverFileStorage(ara::core::InstanceSpecifier) noexcept {
    // Minimal: no-op
    return {};
}

ara::core::Result<void> ResetFileStorage(ara::core::InstanceSpecifier fs_spec) noexcept {
    if (!StorageRegistry::Instance().IsInitialized())
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    auto cfg = StorageRegistry::Instance().Lookup(fs_spec.ToString());
    if (!cfg || cfg->type != StorageType::Files)
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    fs::path base = cfg->base_path;

    std::error_code ec;
    if (!fs::exists(base, ec)) return {};
    for (const auto& entry : fs::recursive_directory_iterator(base, ec)) {
        if (ec) break;
        if (entry.is_regular_file()) fs::remove(entry.path(), ec);
    }
#if defined(__unix__) || defined(__APPLE__)
    fsync_dir_by_path(base.string());
#endif
    return {};
}


} // namespace ara::per
