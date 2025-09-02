#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ara/core/result.hpp>

namespace persistency {

class KeyValueStorageBackend {
public:
    static constexpr size_t kDefaultQuota = 1024 * 1024; // 1MB per storage
    KeyValueStorageBackend(const std::string& base_path, size_t quota = kDefaultQuota);
    ara::core::Result<void> SetValue(const std::string& key, const std::string& value) noexcept;
    ara::core::Result<std::string> GetValue(const std::string& key) const noexcept;
    ara::core::Result<std::vector<std::string>> GetAllKeys() const noexcept;
    ara::core::Result<bool> HasKey(const std::string& key) const noexcept;
    ara::core::Result<void> RemoveKey(const std::string& key) noexcept;
    ara::core::Result<void> SyncToStorage() const noexcept;
    ara::core::Result<void> DiscardPendingChanges() const noexcept;
    size_t GetUsedSpace() const;           // locks mtx_

    size_t GetQuota() const { return quota_; }

private:
    std::string base_path_;
    size_t quota_{kDefaultQuota};
    mutable std::mutex mtx_;

    // NEW: use this only while mtx_ is already held
    size_t GetUsedSpaceNoLock_() const noexcept;
};

} // namespace persistency
