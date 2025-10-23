// ara/com/someip_adapter.cpp — the only file that touches the binding
#include "ara/com/core.hpp"
#include "someip_binding.hpp"          // resolved via PRIVATE include dir: ${CMAKE_SOURCE_DIR}/com
#include <vsomeip/vsomeip.hpp>         // only used in this TU
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace ara::com {

class SomeipAdapter final : public IAdapter {
public:
  // ---- Init / shutdown -----------------------------------------------------
  Errc init(const std::string& app) override {
    std::call_once(once_, [&]{ someip::init(app); });
    return Errc::kOk;
  }
  void shutdown() override { someip::shutdown(); }

  // ---- Discovery / attach --------------------------------------------------
  Errc request_service(ServiceId s, InstanceId i) override {
    someip::request_service(s, i);
    return Errc::kOk;
  }
  void release_service(ServiceId s, InstanceId i) override {
    someip::release_service(s, i); // implement as no-op if not in binding yet
  }

  // ---- RPC -----------------------------------------------------------------
  Errc send_request(ServiceId s, InstanceId i, MethodId m,
                    const std::string& payload, Resp cb) override {
    // fire-and-forget for now; upgrade when binding returns replies
    someip::send_request(s, i, m, payload);
    if (cb) cb(Errc::kOk, {});
    return Errc::kOk;
  }

  // ---- Events (client) -----------------------------------------------------
  // Updated to handle problem with someip implicitly subscribing to all events
  SubscriptionToken subscribe_event(ServiceId s, InstanceId i,
                                    EventGroupId g, EventId e,
                                    EventCb cb) override {
    const auto token = SubscriptionToken{++next_token_};

    {
      std::lock_guard lk(mu_);
      subs_[Key{s,i,e}][token.value] = std::move(cb);
      token_meta_[token.value] = SubMeta{s,i,g,e};
    }

    ensure_dispatcher_installed();

    // CHANGE: explicitly request this event before subscribing
    someip::request_event(s, i, e, {g}, /*reliable*/true);

    // Subscribe only to the requested group for this event
    someip::subscribe_to_event(s, i, g, e);

    return token;
  }

  void unsubscribe_event(SubscriptionToken t) override {
    SubMeta meta{};
    {
      std::lock_guard lk(mu_);
      auto it_meta = token_meta_.find(t.value);
      if (it_meta == token_meta_.end()) return;
      meta = it_meta->second;
      token_meta_.erase(it_meta);

      auto it = subs_.find(Key{meta.s, meta.i, meta.e});
      if (it != subs_.end()) {
        it->second.erase(t.value);
        if (it->second.empty()) {
          subs_.erase(it);
          // Last subscriber gone: tear everything down for this event
          someip::unsubscribe_event(meta.s, meta.i, meta.g, meta.e);
          // CHANGE: release event so vsomeip stops routing frames to us
          someip::release_event(meta.s, meta.i, meta.e);
        }
      }
    }
  }
  void ensure_dispatcher_installed() {
    std::call_once(dispatch_once_, [&]{
      someip::register_notification_handler(
        [this](uint16_t sid, uint16_t iid, uint16_t evid,
              const std::string& payload,
              std::shared_ptr<vsomeip::message> /*unused*/) {
          std::lock_guard lk(mu_);
          auto it = subs_.find(Key{sid, iid, evid});
          if (it == subs_.end()) return;            // no subscribers for this event → drop
          for (auto& [_, cb] : it->second) if (cb) cb(payload);
        }
      );
    });
  }


  // ---- Server side ---------------------------------------------------------
  Errc offer_service(ServiceId s, InstanceId i) override {
    // If you add a 2-arg overload in the binding, call that instead.
    someip::offer_service(s, i, /*event_id*/0, /*group*/0);
    return Errc::kOk;
  }
  void stop_offer_service(ServiceId s, InstanceId i) override {
    someip::stop_offer_service(s, i);
  }

  Errc send_notification(ServiceId s, InstanceId i, EventId e,
                         const std::string& payload) override {
    someip::send_notification(s, i, e, payload);
    return Errc::kOk;
  }

  // ---- Availability bridge -------------------------------------------------
  SubscriptionToken on_availability(ServiceId s, InstanceId i, AvCb cb) override {
    auto tok = someip::register_availability_handler(
      [=](uint16_t ss, uint16_t ii, bool up){
        if (ss==s && ii==i && cb)
          cb(up ? Availability::kAvailable : Availability::kNotAvailable);
      });
    return SubscriptionToken{tok};
  }
  void remove_availability_handler(SubscriptionToken t) override {
    someip::remove_availability_handler(t.value);
  }

  // ---- Singleton access ----------------------------------------------------
  static SomeipAdapter& instance() { static SomeipAdapter a; return a; }

private:
  struct Key {
    ServiceId s; InstanceId i; EventId e;
    bool operator==(const Key& o) const { return s==o.s && i==o.i && e==o.e; }
  };
  struct KeyHash {
    size_t operator()(const Key& k) const {
      return (static_cast<size_t>(k.s) << 16)
           ^ (static_cast<size_t>(k.i) << 1)
           ^ static_cast<size_t>(k.e);
    }
  };
  struct SubMeta { ServiceId s; InstanceId i; EventGroupId g; EventId e; };

  void ensure_dispatcher_installed() {
  std::call_once(dispatch_once_, [&]{
    someip::register_notification_handler(
      [this](uint16_t sid, uint16_t iid, uint16_t evid,
             const std::string& payload,
             std::shared_ptr<vsomeip::message> /*unused*/) {
        std::lock_guard lk(mu_);
        auto it = subs_.find(Key{sid, iid, evid});
        if (it == subs_.end()) return;
        for (auto& [_, cb] : it->second) if (cb) cb(payload);
      }
    );
  });
}


  std::once_flag once_, dispatch_once_;
  std::mutex mu_;
  std::atomic_uint64_t next_token_{0};

  std::unordered_map<Key, std::unordered_map<std::uint64_t, EventCb>, KeyHash> subs_;
  std::unordered_map<std::uint64_t, SubMeta> token_meta_;
};

// Public accessor for the adapter singleton
IAdapter& GetSomeipAdapter() { return SomeipAdapter::instance(); }

} // namespace ara::com
