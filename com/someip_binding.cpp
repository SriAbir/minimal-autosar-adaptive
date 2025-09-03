#include "someip_binding.hpp"
#include <iostream>
#include <vector>
#include <mutex>
#include <thread> 

namespace someip {

std::shared_ptr<vsomeip::application> app;
std::mutex handler_mutex;
std::function<void(const std::string&)> global_handler;
static RpcHandler rpc_handler; // for health manager

void init(const std::string& app_name) {
    app = vsomeip::runtime::get()->create_application(app_name);

    if (!app->init()) {
        std::cerr << "[someip] Failed to initialize application" << std::endl;
        std::exit(1);
    }

    app->register_message_handler(
        vsomeip::ANY_SERVICE,
        vsomeip::ANY_INSTANCE,
        vsomeip::ANY_METHOD,
        [](std::shared_ptr<vsomeip::message> msg) {
            auto pl = msg->get_payload();
            std::string payload;
            if (pl && pl->get_length())
                payload.assign(reinterpret_cast<const char*>(pl->get_data()), pl->get_length());

            // Structured path: PHM server (EM) can switch on S/I/M
            {
                std::lock_guard<std::mutex> lock(handler_mutex);
                if (rpc_handler) {
                    rpc_handler(msg->get_service(),
                                msg->get_instance(),
                                msg->get_method(),
                                payload,
                                msg);
                    return;
                }
            }

            // Fallback (to the old global handler, still supported)
            {
                std::lock_guard<std::mutex> lock(handler_mutex);
                if (global_handler) global_handler(payload);
            }
        }
    );

    app->register_availability_handler(
    vsomeip::ANY_SERVICE,
    vsomeip::ANY_INSTANCE,
    [](vsomeip::service_t service,
       vsomeip::instance_t instance,
       bool is_available) {
        if (is_available) {
            std::cout << "[shim] Service " << std::hex << service << ":" << instance << " is available" << std::endl;
            app->subscribe(service, instance, 0x01);  // now it's safe
        }
    }
);


    //app->start();
     std::thread vsomeip_thread([] {
        
        app->start();
    });
    vsomeip_thread.detach();
}

//void offer_service(uint16_t service_id, uint16_t instance_id) {
    //app->offer_service(service_id, instance_id);
   // std::cout << "[someip] Offering service " << std::hex << service_id << ":" << instance_id << std::endl;
//}
void offer_service(uint16_t service_id, uint16_t instance_id, uint16_t event_id, uint16_t event_group_id) {
    app->offer_service(service_id, instance_id);

    std::set<vsomeip::eventgroup_t> event_groups;
    event_groups.insert(event_group_id);

    app->offer_event(
        service_id,
        instance_id,
        event_id,
        event_groups,
        vsomeip::event_type_e::ET_FIELD,
        std::chrono::milliseconds::zero(),
        false, // not change resilient
        true   // reliable
    );

    app->subscribe(service_id, instance_id, event_group_id);

    std::cout << "[someip] Offered service " << std::hex << service_id
              << ":" << instance_id << ", event 0x" << event_id
              << ", group 0x" << event_group_id << std::endl;
}

void subscribe_to_event(uint16_t service_id, uint16_t instance_id, uint16_t event_group_id, uint16_t event_id) {
    std::set<vsomeip::eventgroup_t> event_groups = { event_group_id };

    app->request_event(
        service_id,
        instance_id,
        event_id,
        event_groups,
        vsomeip::event_type_e::ET_FIELD,
        vsomeip::reliability_type_e::RT_RELIABLE
    );

    app->subscribe(service_id, instance_id, event_group_id);

    std::cout << "[someip] Requested event 0x" << std::hex << event_id
              << " and subscribed to group 0x" << event_group_id << std::endl;
}




void request_service(uint16_t service_id, uint16_t instance_id) {
    app->request_service(service_id, instance_id);
    std::cout << "[someip] Requesting service " << std::hex << service_id << ":" << instance_id << std::endl;
}

void send_notification(uint16_t service_id, uint16_t instance_id, uint16_t event_id, const std::string& payload) {
    auto payload_ptr = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(payload.begin(), payload.end());
    payload_ptr->set_data(data);

    // Use the correct notify() overload
    app->notify(service_id, instance_id, event_id, payload_ptr, true);  // true = reliable

    std::cout << "[someip] Sent notification (event: 0x" << std::hex << event_id << "): " << payload << std::endl;
}

void register_handler(std::function<void(const std::string&)> handler) {
    std::lock_guard<std::mutex> lock(handler_mutex);
    global_handler = handler;
}

// Implement the RPC handler to handle health manager requests
void register_rpc_handler(RpcHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex);
    rpc_handler = std::move(handler);
}

void send_request(uint16_t service_id, uint16_t instance_id, uint16_t method_id,
                  const std::string& payload) {
    auto req = vsomeip::runtime::get()->create_request();
    req->set_service(service_id);
    req->set_instance(instance_id);
    req->set_method(method_id);
    req->set_reliable(true);

    auto p = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(payload.begin(), payload.end());
    p->set_data(data);
    req->set_payload(p);

    app->send(req);  // fire-and-forget for PHM
}

void send_response(std::shared_ptr<vsomeip::message> request,
                   const std::string& payload) {
    auto resp = vsomeip::runtime::get()->create_response(request);

    auto p = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(payload.begin(), payload.end());
    p->set_data(data);
    resp->set_payload(p);

    app->send(resp);
}

} // namespace someip
