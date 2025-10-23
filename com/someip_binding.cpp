//com/someip_binding.cpp
#include "someip_binding.hpp"
#include <iostream>
#include <vector>
#include <unordered_set>
#include <tuple>
#include <mutex>
#include <thread>
#include <atomic>   
#include <set>
#include <unordered_map>
#include <sstream>
#include <cstdlib>

namespace {
   inline std::set<vsomeip::eventgroup_t>
  to_group_set(std::initializer_list<uint16_t> groups) {
    std::set<vsomeip::eventgroup_t> s;
    for (auto g : groups) s.insert(static_cast<vsomeip::eventgroup_t>(g));
    return s;
  }
}

namespace someip {

std::shared_ptr<vsomeip::application> app;
std::mutex handler_mutex;
std::function<void(const std::string&)> global_handler;
// Changes to handle multiple handlers
static std::mutex notif_mu;
static std::vector<NotifHandler> notif_handlers;
static std::vector<RpcHandler>   rpc_handlers;

// NEW: guard against double init / double start (safe, process-local)
static std::mutex       g_init_mu;
static std::atomic_bool g_started{false};
static std::string      g_app_name;

// NEW: make “auto-subscribe on availability” optional (default OFF)
static std::atomic_bool g_auto_subscribe{false};
static std::atomic<uint16_t> g_default_event_group{0x0001};

//Additions to help hide someip behind ara::com
static std::mutex avail_mu;
static std::atomic_uint64_t avail_next{0};
static std::unordered_map<std::uint64_t, AvailabilityHandler> avail_cbs;

//Fixing connection issues with registry
static std::mutex g_offer_mu;
static std::unordered_set<std::uint64_t> g_offered_events;
static inline std::uint64_t make_key(uint16_t s, uint16_t i, uint16_t e) {
   return (static_cast<std::uint64_t>(s) << 32)
         | (static_cast<std::uint64_t>(i) << 16)
         | static_cast<std::uint64_t>(e);
  }

//Better shutdown behavior
static std::thread g_vsomeip_thread;


// Event handling additions

static inline vsomeip::reliability_type_e to_rel(bool reliable) {
  return reliable
    ? vsomeip::reliability_type_e::RT_RELIABLE
    : vsomeip::reliability_type_e::RT_UNRELIABLE;
}

void request_event(uint16_t s, uint16_t i, uint16_t e,
                   std::initializer_list<uint16_t> groups,
                   bool reliable) {
  auto gs = to_group_set(groups);

  // vsomeip: request_event(service, instance, event, groupset, reliable)
  app->request_event(s, i, e, gs, vsomeip::event_type_e::ET_EVENT, to_rel(reliable));
}

void release_event(uint16_t s, uint16_t i, uint16_t e) {
  app->release_event(s, i, e);
}

void enable_auto_subscribe(bool enable, uint16_t event_group_id) {
    g_auto_subscribe.store(enable, std::memory_order_relaxed);
    g_default_event_group.store(event_group_id, std::memory_order_relaxed);
}

void init(const std::string& app_name) {
    std::lock_guard<std::mutex> lk(g_init_mu);

    // Idempotent: if already initialized, do nothing
    if (app) {
        if (g_app_name != app_name) {
            std::cerr << "[someip] already initialized as '" << g_app_name
                      << "', ignoring init('" << app_name << "')\n";
        }
        return;
    }

    g_app_name = app_name;
    app = vsomeip::runtime::get()->create_application(app_name);

    if (!app->init()) {
        std::cerr << "[someip] Failed to initialize application\n";
        std::exit(1);
    }

    app->register_message_handler(
        vsomeip::ANY_SERVICE,
        vsomeip::ANY_INSTANCE,
        vsomeip::ANY_METHOD,
        [](std::shared_ptr<vsomeip::message> msg) {
            // Extract payload once
            std::string payload;
            if (auto pl = msg->get_payload()) {
                auto len = pl->get_length();
                if (len) payload.assign(reinterpret_cast<const char*>(pl->get_data()), len);
            }

            const bool is_notif = (msg->get_message_type() == vsomeip::message_type_e::MT_NOTIFICATION);

            if (is_notif) {
                // fan-out to all notification handlers (ara::com adapter lives here)
                std::vector<NotifHandler> cbs;
                { std::lock_guard<std::mutex> lk(notif_mu); cbs = notif_handlers; }
                for (auto &cb : cbs) if (cb)
                    cb(msg->get_service(), msg->get_instance(), msg->get_method(), payload, msg);

                // (optional) legacy fallback
                std::lock_guard<std::mutex> lk(handler_mutex);
                if (cbs.empty() && global_handler) global_handler(payload);
            } else {
                // requests/responses go to RPC handlers (e.g., EM’s PHM server)
                std::vector<RpcHandler> cbs;
                { std::lock_guard<std::mutex> lk(handler_mutex); cbs = rpc_handlers; }
                for (auto &cb : cbs) if (cb)
                    cb(msg->get_service(), msg->get_instance(), msg->get_method(), payload, msg);
            }
        }
    );

    app->register_availability_handler(
    vsomeip::ANY_SERVICE,
    vsomeip::ANY_INSTANCE,
    [](vsomeip::service_t service,
       vsomeip::instance_t instance,
       bool is_available) {

        if (is_available && g_auto_subscribe.load(std::memory_order_relaxed)) {
            auto eg = g_default_event_group.load(std::memory_order_relaxed);
            try { app->subscribe(service, instance, eg); } catch (...) {}
        }

        // NEW: notify external listeners
        std::vector<AvailabilityHandler> cbs;
        {
            std::lock_guard<std::mutex> lk(avail_mu);
            for (auto &kv : avail_cbs) cbs.push_back(kv.second);
        }
        for (auto &cb : cbs) if (cb) cb(service, instance, is_available);
    }
);
    // Auto-request events from env (format: "svc:inst:event[@group],svc:inst:event...")
    if (const char* env = std::getenv("SOMEIP_REQUEST_EVENTS")) {
        std::string spec(env);
        std::istringstream iss(spec);
        std::string tok;

        auto parse_u32 = [](const std::string& s) -> uint32_t {
            try { return static_cast<uint32_t>(std::stoul(s, nullptr, 0)); }
            catch (...) { return 0; }
        };

        while (std::getline(iss, tok, ',')) {
            if (tok.empty()) continue;

            // optional "@group"
            std::string left = tok, grp;
            if (auto at = tok.find('@'); at != std::string::npos) {
                left = tok.substr(0, at);
                grp  = tok.substr(at + 1);
            }

            // split "svc:inst:event"
            std::istringstream lss(left);
            std::string s, i, e;
            if (std::getline(lss, s, ':') && std::getline(lss, i, ':') && std::getline(lss, e, ':')) {
                uint16_t svc = static_cast<uint16_t>(parse_u32(s));
                uint16_t inst= static_cast<uint16_t>(parse_u32(i));
                uint16_t evt = static_cast<uint16_t>(parse_u32(e));
                uint16_t eg  = grp.empty()
                    ? g_default_event_group.load(std::memory_order_relaxed)
                    : static_cast<uint16_t>(parse_u32(grp));

                try {
                    std::set<vsomeip::eventgroup_t> groups{eg};
                    app->request_event(svc, inst, evt, groups,
                                    vsomeip::event_type_e::ET_EVENT,
                                    vsomeip::reliability_type_e::RT_RELIABLE);
                    app->subscribe(svc, inst, eg);
                    std::cout << "[someip] auto-requested 0x" << std::hex << evt
                            << " on " << svc << ":" << inst
                            << " (group 0x" << eg << ")\n";
                } catch (...) {
                    std::cerr << "[someip] auto-request failed for token: " << tok << "\n";
                }
            }
        }
}


    // Start vsomeip only once per process
    if (!g_started.exchange(true)) {
        g_vsomeip_thread = std::thread([] { app->start(); });
    }
}

void shutdown() {
    if (!app) return;
    try { app->stop(); } catch (...) {}
    if (g_vsomeip_thread.joinable())
        g_vsomeip_thread.join();
    g_started = false;
}

void offer_service(uint16_t service_id, uint16_t instance_id, uint16_t event_id, uint16_t event_group_id) {
    app->offer_service(service_id, instance_id);

    //std::set<vsomeip::eventgroup_t> event_groups;
    //event_groups.insert(event_group_id);

    if(event_id != 0){
        std::set<vsomeip::eventgroup_t> event_groups{event_group_id};
        app->offer_event(
            service_id,
            instance_id,
            event_id,
            event_groups,
            vsomeip::event_type_e::ET_EVENT,
            std::chrono::milliseconds::zero(),
            false, // not change resilient
            true   // reliable
        );
    }
    //app->subscribe(service_id, instance_id, event_group_id);

    std::cout << "[someip] Offered service " << std::hex << service_id
              << ":" << instance_id << ", event 0x" << event_id
              << ", group 0x" << event_group_id << std::endl;
}

void subscribe_to_event(uint16_t service_id, uint16_t instance_id, uint16_t event_group_id, uint16_t event_id) {
    
    std::set<vsomeip::eventgroup_t> event_groups = { event_group_id };

    app->subscribe(service_id, instance_id, event_group_id);

    std::cout << "[someip] Subscribed to group 0x" << std::hex << event_group_id << std::endl;
}

void request_service(uint16_t service_id, uint16_t instance_id) {
    app->request_service(service_id, instance_id);
    std::cout << "[someip] Requesting service " << std::hex << service_id << ":" << instance_id << std::endl;
}

void send_notification(uint16_t service_id, uint16_t instance_id, uint16_t event_id, const std::string& payload) {
    
    // Ensure the event is offered at least once (lazy registration).
    {
        std::lock_guard<std::mutex> lk(g_offer_mu);
        auto key = make_key(service_id, instance_id, event_id);
        if (!g_offered_events.count(key)) {
            std::set<vsomeip::eventgroup_t> egs{
                g_default_event_group.load(std::memory_order_relaxed)  // defaults to 0x0001
            };
            app->offer_event(
                service_id,
                instance_id,
                event_id,
                egs,
                vsomeip::event_type_e::ET_EVENT,
                std::chrono::milliseconds::zero(),
                false, // not change resilient
                true   // reliable
            );
            g_offered_events.insert(key);
        }
    }
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
    rpc_handlers.push_back(std::move(handler));
}

//Implementing a new notification handle to allow multiple handlers
void register_notification_handler(NotifHandler handler) {
    std::lock_guard<std::mutex> lock(notif_mu);
    notif_handlers.push_back(std::move(handler));
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

//Additions to help hide someip behind ara::com
void release_service(uint16_t s, uint16_t i) {
    if (app) app->release_service(s, i);
}
void stop_offer_service(uint16_t s, uint16_t i) {
    if (app) app->stop_offer_service(s, i);
}
void unsubscribe_event(uint16_t s, uint16_t i, uint16_t g, uint16_t e) {
    if (!app) return;
    try {
        app->unsubscribe(s, i, g);
    } catch (...) {}
}

AvailabilityToken register_availability_handler(AvailabilityHandler cb) {
    const auto id = ++avail_next;
    std::lock_guard<std::mutex> lk(avail_mu);
    avail_cbs[id] = std::move(cb);
    return id;
}
void remove_availability_handler(AvailabilityToken tok) {
    std::lock_guard<std::mutex> lk(avail_mu);
    avail_cbs.erase(tok);
}


} // namespace someip
