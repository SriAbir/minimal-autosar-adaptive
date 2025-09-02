#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <ara/core/result.hpp>
#include <ara/core/instance_specifier.hpp>

namespace ara::per {

class FileStorage {
public:
    explicit FileStorage(const std::string& base_path, size_t quota_bytes = SIZE_MAX);

    ara::core::Result<void> WriteFile(std::string_view path,
                                      const std::vector<uint8_t>& data) noexcept;
    ara::core::Result<std::vector<uint8_t>> ReadFile(std::string_view path) const noexcept;
    ara::core::Result<void> RemoveFile(std::string_view path) noexcept;
    ara::core::Result<std::vector<std::string>> ListFiles() const noexcept;

    size_t GetUsedSpace() const;

    ara::core::Result<void> SyncToStorage() const noexcept;
    ara::core::Result<void> DiscardPendingChanges() const noexcept;

private:
    std::string base_path_;
    size_t quota_;
    mutable std::mutex mtx_;
};

using SharedFileHandle = std::shared_ptr<FileStorage>;

// Free functions per ara::per facade
ara::core::Result<SharedFileHandle>
OpenFileStorage(ara::core::InstanceSpecifier fs, size_t quota_bytes = SIZE_MAX) noexcept;

ara::core::Result<void> RecoverFileStorage(ara::core::InstanceSpecifier fs) noexcept;
ara::core::Result<void> ResetFileStorage(ara::core::InstanceSpecifier fs) noexcept;

} // namespace ara::per
