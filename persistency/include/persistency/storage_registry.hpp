#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <ara/core/result.hpp>

namespace persistency {

enum class StorageType { Kv, Files };

struct StorageConfig {
    StorageType type;
    std::string base_path;
    size_t      quota_bytes;
    bool        recover_on_start{false};
    // optional: reset policy, reserved_headroom, etc.
};

class StorageRegistry {
public:
    static StorageRegistry& Instance();

    // Load from JSON/YAML manifest on startup
    ara::core::Result<void> InitFromFile(const std::string& config_path) noexcept;

    // Lookup by InstanceSpecifier string
    std::optional<StorageConfig> Lookup(const std::string& instance) const;

    bool IsInitialized() const noexcept { return inited_.load(std::memory_order_acquire); }
    void Clear() noexcept;

private:
    StorageRegistry() = default;
    StorageRegistry(const StorageRegistry&) = delete;
    StorageRegistry& operator=(const StorageRegistry&) = delete;

    mutable std::mutex mtx_;
    std::unordered_map<std::string, StorageConfig> map_;
    std::atomic<bool> inited_{false};
};

} // namespace persistency
