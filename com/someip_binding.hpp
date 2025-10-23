//com/someip_binding.hpp
#pragma once
#include <string>
#include <functional>
#include <vsomeip/vsomeip.hpp>

namespace someip {

void init(const std::string& app_name);
//void offer_service(uint16_t service_id, uint16_t instance_id);
void offer_service(uint16_t service_id, uint16_t instance_id, uint16_t event_id, uint16_t event_group_id);
void subscribe_to_event(uint16_t service_id, uint16_t instance_id, uint16_t event_group_id, uint16_t event_id);

// Adding functions to handle events. Fixing problem of implicitly subscribing to all events
void request_event(uint16_t s, uint16_t i, uint16_t e, std::initializer_list<uint16_t> groups, bool reliable);
void release_event(uint16_t s, uint16_t i, uint16_t e);

void request_service(uint16_t service_id, uint16_t instance_id);
void send_notification(uint16_t service_id, uint16_t instance_id, uint16_t event_id, const std::string& payload);
void register_handler(std::function<void(const std::string&)> handler);

//TODO: Add the following if needed
//void subscribe_to_event(uint16_t service_id, uint16_t instance_id, uint16_t event_group_id);
//void request_event(uint16_t service_id, uint16_t instance_id,uint16_t event_id,uint16_t eventgroup_id,bool reliable);

//Trying to fix someip issue. Making init() idempotent
void enable_auto_subscribe(bool enable, uint16_t event_group_id = 0x0001);

// Additions below for health manager
void send_request(uint16_t service_id, uint16_t instance_id, uint16_t method_id,
                  const std::string& payload);

// Server-side (and for clients who care about replies) structured handler:
using RpcHandler = std::function<void(uint16_t service_id,
                                      uint16_t instance_id,
                                      uint16_t method_id,
                                      const std::string& payload,
                                      std::shared_ptr<vsomeip::message> req)>;

// Adding notification handler type
using NotifHandler = std::function<void(uint16_t service,
                                        uint16_t instance,
                                        uint16_t event_id,
                                        const std::string& payload,
                                        std::shared_ptr<vsomeip::message> msg)>;


void register_rpc_handler(RpcHandler handler);
//New notification handler registration to allow multiple handlers
void register_notification_handler(NotifHandler handler);

// Optional helper to send an empty or payloaded ACK back:
void send_response(std::shared_ptr<vsomeip::message> request,
                   const std::string& payload = "");

// Clean detach paths
void release_service(uint16_t service_id, uint16_t instance_id);
void stop_offer_service(uint16_t service_id, uint16_t instance_id);
void unsubscribe_event(uint16_t service_id, uint16_t instance_id,
                       uint16_t event_group_id, uint16_t event_id);

// Availability fan-out (optional but useful)
using AvailabilityHandler = std::function<void(uint16_t service,
                                               uint16_t instance,
                                               bool available)>;
using AvailabilityToken = std::uint64_t;

AvailabilityToken register_availability_handler(AvailabilityHandler cb);
void remove_availability_handler(AvailabilityToken tok);

void shutdown();
}
