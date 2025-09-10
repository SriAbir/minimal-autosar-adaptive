// ara/com/core.hpp  — public, transport-agnostic
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace ara::com {

using ServiceId    = std::uint16_t;
using InstanceId   = std::uint16_t;
using MethodId     = std::uint16_t;
using EventId      = std::uint16_t;
using EventGroupId = std::uint16_t;

enum class Availability { kUnknown, kNotAvailable, kAvailable };
enum class Errc { kOk=0, kNotFound, kBusy, kTimeout, kTransportError, kInvalidArg };

struct SubscriptionToken { std::uint64_t value{0}; };

// ---- Adapter (implemented once; SOME/IP, DDS, …) ----
struct IAdapter {
  virtual ~IAdapter() = default;
  virtual Errc init(const std::string& app) = 0;
  virtual void shutdown() = 0;

  virtual Errc request_service(ServiceId s, InstanceId i) = 0;
  virtual void release_service(ServiceId s, InstanceId i) = 0;

  using Resp = std::function<void(Errc, const std::string&)>;
  virtual Errc send_request(ServiceId s, InstanceId i, MethodId m,
                            const std::string& payload, Resp cb) = 0;

  using EventCb = std::function<void(const std::string&)>;
  virtual SubscriptionToken subscribe_event(ServiceId s, InstanceId i,
                                            EventGroupId g, EventId e, EventCb cb) = 0;
  virtual void unsubscribe_event(SubscriptionToken) = 0;

  virtual Errc offer_service(ServiceId s, InstanceId i) = 0;
  virtual void stop_offer_service(ServiceId s, InstanceId i) = 0;
  virtual Errc send_notification(ServiceId s, InstanceId i, EventId e,
                                 const std::string& payload) = 0;

  using AvCb = std::function<void(Availability)>;
  virtual SubscriptionToken on_availability(ServiceId s, InstanceId i, AvCb cb) = 0;
  virtual void remove_availability_handler(SubscriptionToken) = 0;
};

// A trivial runtime holder that all Proxies/Skeletons share
class Runtime {
public:
  explicit Runtime(IAdapter& a) : adapter_(a) {}
  IAdapter& adapter() { return adapter_; }
private:
  IAdapter& adapter_;
};

// ---- Default codec hooks (can be specialized per type) ----
template<typename T> struct Codec {
  static std::string serialize(const T&);     // you can specialize for your types
  static T deserialize(const std::string&);
};
// float specialization (simple text for demo)
template<> inline std::string Codec<float>::serialize(const float& v){
  return std::to_string(v);
}
template<> inline float Codec<float>::deserialize(const std::string& s){
  try { return std::stof(s); } catch(...) { return 0.f; }
}

// ---- Generic Proxy/Skeleton parameterized by a descriptor ----
template<typename Desc>
class Proxy {
public:
  using This = Proxy<Desc>;
  explicit Proxy(Runtime& rt, std::string app_name = Desc::kDefaultClient)
    : rt_(rt), app_(std::move(app_name)) { rt_.adapter().init(app_); }

  bool RequestService() {
    return rt_.adapter().request_service(Desc::kServiceId, Desc::kInstanceId) == Errc::kOk;
  }
  void ReleaseService() { rt_.adapter().release_service(Desc::kServiceId, Desc::kInstanceId); }

  // Subscribe to an event given its descriptor entry
  template<typename E>
  SubscriptionToken Subscribe(typename E::Callback cb) {
    return rt_.adapter().subscribe_event(
      Desc::kServiceId, Desc::kInstanceId, E::kGroup, E::kId,
      [cb = std::move(cb)](const std::string& bytes){
        if (cb) cb(Codec<typename E::Payload>::deserialize(bytes));
      }
    );
  }

  // Call a method (async)
  template<typename M>
  Errc Call(const typename M::Request& req,
            std::function<void(Errc, typename M::Response)> on_done) {
    return rt_.adapter().send_request(
      Desc::kServiceId, Desc::kInstanceId, M::kId,
      Codec<typename M::Request>::serialize(req),
      [on_done = std::move(on_done)](Errc ec, const std::string& bytes){
        using R = typename M::Response;
        if (on_done) on_done(ec, (ec==Errc::kOk) ? Codec<R>::deserialize(bytes) : R{});
      }
    );
  }

private:
  Runtime& rt_;
  std::string app_;
};

template<typename Desc>
class Skeleton {
public:
  explicit Skeleton(Runtime& rt, std::string app_name = Desc::kDefaultServer)
    : rt_(rt), app_(std::move(app_name)) { rt_.adapter().init(app_); }

  void Offer()  { rt_.adapter().offer_service(Desc::kServiceId, Desc::kInstanceId); }
  void Stop()   { rt_.adapter().stop_offer_service(Desc::kServiceId, Desc::kInstanceId); }

  template<typename E>
  Errc Notify(const typename E::Payload& v) {
    return rt_.adapter().send_notification(
      Desc::kServiceId, Desc::kInstanceId, E::kId,
      Codec<typename E::Payload>::serialize(v));
  }

private:
  Runtime& rt_;
  std::string app_;
};

} // namespace ara::com
