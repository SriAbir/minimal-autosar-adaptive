#include <persistency/storage_registry.hpp>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using nlohmann::json;

namespace persistency {

StorageRegistry& StorageRegistry::Instance() {
    static StorageRegistry r;
    return r;
}

static StorageType ParseType(const std::string& s) {
    if (s == "kv")    return StorageType::Kv;
    if (s == "files") return StorageType::Files;
    return StorageType::Files; // default minimal
}

ara::core::Result<void> StorageRegistry::InitFromFile(const std::string& path) noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    map_.clear();

    std::ifstream in(path);
    if (!in) return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);

    json j;
    try { in >> j; } catch (...) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kCorruption);
    }

    try {
        for (const auto& s : j.at("storages")) {
            StorageConfig cfg;
            const std::string inst = s.at("instance_spec").get<std::string>();
            cfg.type         = ParseType(s.value("type","files"));
            cfg.base_path    = s.at("base_path").get<std::string>();
            cfg.quota_bytes  = s.value("quota_bytes", static_cast<size_t>(-1));
            cfg.recover_on_start = s.value("recover_on_start", false);

            // Minimal hardening: ensure directory exists
            std::error_code ec;
            fs::create_directories(cfg.base_path, ec);

            map_.emplace(inst, std::move(cfg));
        }
    } catch (...) {
        map_.clear();
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kCorruption);
    }

    inited_.store(true, std::memory_order_release);
    return {};
}

std::optional<StorageConfig> StorageRegistry::Lookup(const std::string& instance) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = map_.find(instance);
    if (it == map_.end()) return std::nullopt;
    return it->second;
}

void StorageRegistry::Clear() noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    map_.clear();
    inited_.store(false, std::memory_order_release);
}

} // namespace platform::persistency
