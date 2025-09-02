#include <ara/per/key_value_storage.hpp>
#include <persistency/key_value_storage_backend.hpp>
#include <persistency/storage_registry.hpp>
#include <memory>
#include <filesystem>
#include <iostream>

namespace ara::per {

ara::core::Result<SharedHandle>
OpenKeyValueStorage(ara::core::InstanceSpecifier kvs) noexcept {
    auto& reg = ::persistency::StorageRegistry::Instance();
    
    if (!reg.IsInitialized()) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    }

    auto cfg = reg.Lookup(kvs.ToString());
    if (!cfg || cfg->type != ::persistency::StorageType::Kv) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    }

    auto backend = std::make_shared<::persistency::KeyValueStorageBackend>(
        cfg->base_path, cfg->quota_bytes
    );
    return std::make_shared<KeyValueStorage>(backend);
}

ara::core::Result<void>
ResetKeyValueStorage(ara::core::InstanceSpecifier kvs) noexcept {
    namespace fs = std::filesystem;
    auto& reg = ::persistency::StorageRegistry::Instance();
    if (!reg.IsInitialized()) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kUnknown);
    }

    auto cfg = reg.Lookup(kvs.ToString());
    if (!cfg || cfg->type != ::persistency::StorageType::Kv) {
        return ara::core::ErrorCode(ara::core::PersistencyErrc::kNotFound);
    }

    std::error_code ec;
    fs::path base = cfg->base_path;
    if (!fs::exists(base, ec)) return {};
    for (const auto& entry : fs::directory_iterator(base, ec)) {
        if (ec) break;
        if (entry.is_regular_file()) fs::remove(entry, ec);
    }
    return {};
}

} // namespace ara::per
