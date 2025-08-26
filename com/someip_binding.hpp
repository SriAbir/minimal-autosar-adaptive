#pragma once
#include <string>
#include <functional>
#include <vsomeip/vsomeip.hpp>

namespace someip {

void init(const std::string& app_name);
//void offer_service(uint16_t service_id, uint16_t instance_id);
void offer_service(uint16_t service_id, uint16_t instance_id, uint16_t event_id, uint16_t event_group_id);
void subscribe_to_event(uint16_t service_id, uint16_t instance_id, uint16_t event_group_id, uint16_t event_id);

void request_service(uint16_t service_id, uint16_t instance_id);
void send_notification(uint16_t service_id, uint16_t instance_id, uint16_t method_id, const std::string& payload);
void register_handler(std::function<void(const std::string&)> handler);
//void subscribe_to_event(uint16_t service_id, uint16_t instance_id, uint16_t event_group_id);
void request_event(uint16_t service_id, uint16_t instance_id,uint16_t event_id,uint16_t eventgroup_id,bool reliable);

}
