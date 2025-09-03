#pragma once
#include <string>
#include <cstdint>

namespace ara { namespace phm {

class SupervisionClient {
public:
    explicit SupervisionClient(const std::string& app_name); // used by vsomeip init
    void Connect();  // request_service()
    void ReportAlive() noexcept;
    void ReportCheckpoint(std::uint32_t id) noexcept;

private:
    std::string app_name_;
};

}} // namespace ara::phm