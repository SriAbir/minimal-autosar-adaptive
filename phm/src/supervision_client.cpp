#include <ara/phm/supervision_client.hpp>
#include "someip_binding.hpp"
#include <phm/phm_ids.hpp>
#include <arpa/inet.h> // htonl

using namespace ara::phm;

SupervisionClient::SupervisionClient(const std::string& app_name)
    : app_name_(app_name) {
    someip::init(app_name_); // creates & starts vsomeip app thread
}

void SupervisionClient::Connect() {
    someip::request_service(phm_ids::kService, phm_ids::kInstance);
}

void SupervisionClient::ReportAlive() noexcept {
    someip::send_request(phm_ids::kService, phm_ids::kInstance, phm_ids::kAlive, "");
}

void SupervisionClient::ReportCheckpoint(std::uint32_t id) noexcept {
    std::uint32_t net = htonl(id);
    const char* p = reinterpret_cast<const char*>(&net);
    std::string payload(p, p + sizeof(net));
    someip::send_request(phm_ids::kService, phm_ids::kInstance, phm_ids::kCheckpoint, payload);
}