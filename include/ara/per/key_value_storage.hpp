
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <string_view>
#include <sstream>
#include <ara/core/result.hpp>
#include <ara/core/instance_specifier.hpp>
#include <persistency/key_value_storage_backend.hpp>

namespace ara::per {

class KeyValueStorage {
public:
    explicit KeyValueStorage(std::shared_ptr<persistency::KeyValueStorageBackend> backend)
        : backend_(std::move(backend)) {}

    template<class T>
    ara::core::Result<void> SetValue(ara::core::StringView key, const T& value) noexcept {
        std::ostringstream oss;
        oss << value;
        return backend_->SetValue(std::string(key), oss.str());
    }

    ara::core::Result<void> SetValue(ara::core::StringView key, const std::string& value) noexcept {
        return backend_->SetValue(std::string(key), value);
    }

    template<class T>
    ara::core::Result<T> GetValue(ara::core::StringView key) const noexcept {
        auto r = backend_->GetValue(std::string(key));
        if (!r.HasValue()) return r.Error();
        std::istringstream iss(r.Value());
        T out{};
        iss >> out;
        if (iss.fail())
            return ara::core::ErrorCode(ara::core::PersistencyErrc::kCorruption);
        return out;
    }

    ara::core::Result<std::string> GetValueString(ara::core::StringView key) const noexcept {
        return backend_->GetValue(std::string(key));
    }

    ara::core::Result<ara::core::Vector<ara::core::String>> GetAllKeys() const noexcept {
        auto r = backend_->GetAllKeys();
        if (!r.HasValue()) return r.Error();
        return r.Value();
    }

    ara::core::Result<bool> HasKey(ara::core::StringView key) const noexcept {
        return backend_->HasKey(std::string(key));
    }

    ara::core::Result<void> RemoveKey(ara::core::StringView key) noexcept {
        return backend_->RemoveKey(std::string(key));
    }

    ara::core::Result<void> SyncToStorage() const noexcept { return backend_->SyncToStorage(); }
    ara::core::Result<void> DiscardPendingChanges() const noexcept { return backend_->DiscardPendingChanges(); }

private:
    std::shared_ptr<persistency::KeyValueStorageBackend> backend_;
};

using SharedHandle = std::shared_ptr<KeyValueStorage>;
ara::core::Result<SharedHandle> OpenKeyValueStorage(ara::core::InstanceSpecifier kvs) noexcept;
ara::core::Result<void> RecoverKeyValueStorage(ara::core::InstanceSpecifier kvs) noexcept;
ara::core::Result<void> ResetKeyValueStorage(ara::core::InstanceSpecifier kvs) noexcept;

} // namespace ara::per

