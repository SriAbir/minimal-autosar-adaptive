#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <string>
#include "log.hpp"
#include "sinks_console.hpp"
#include "com/someip_binding.hpp"
#include "persistency/key_value_storage_backend.hpp"
#include "ara/per/key_value_storage.hpp"
#include "ara/phm/supervision_client.hpp"

namespace demo_if {
  constexpr std::uint16_t kServiceId   = 0x1234;
  constexpr std::uint16_t kInstanceId  = 0x0001;
  constexpr std::uint16_t kEventId     = 0x8001;
  constexpr std::uint16_t kEventGroup  = 0x0001; // must match provider + vsomeip.json
}

static std::atomic<bool> running{true};
static void on_sig(int){ running=false; }

int main() {
  std::signal(SIGTERM, on_sig);

  // logging
  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","speed_client");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("SPD");

  // persistency
  auto backend = std::make_shared<persistency::KeyValueStorageBackend>("/var/adaptive/per/demo");
  ara::per::KeyValueStorage kv(backend);
  float max_speed = 90.0f;
  if (auto r = kv.GetValue<float>("max_allowed_speed"); r.HasValue()) max_speed = r.Value();
  else kv.SetValue("max_allowed_speed", std::to_string(max_speed));

  // PHM
  ara::phm::SupervisionClient phm("speed_client");

  // SOME/IP via your wrapper
  someip::init("speed_client"); // starts vsomeip in its own thread
  someip::request_service(demo_if::kServiceId, demo_if::kInstanceId);
  someip::subscribe_to_event(demo_if::kServiceId, demo_if::kInstanceId,
                             demo_if::kEventGroup, demo_if::kEventId);

  std::atomic<int> missed_ticks{0};

  // Use the structured RPC handler so we can filter on S/I/M and message type
  someip::register_rpc_handler(
    [&](uint16_t sid, uint16_t iid, uint16_t mid,
        const std::string& payload,
        std::shared_ptr<vsomeip_v3::message> req) {

      // Events arrive as NOTIFICATION with method == event id
      if (sid != demo_if::kServiceId || iid != demo_if::kInstanceId || mid != demo_if::kEventId)
        return;
      if (req->get_message_type() != vsomeip_v3::message_type_e::MT_NOTIFICATION)
        return;

      float speed = 0.f;
      try { speed = std::stof(payload); } catch(...) {}

      kv.SetValue("last_speed", std::to_string(speed));
      if (speed > max_speed)
        ARA_LOGWARN(lg, "Speed {} exceeds threshold {}!", speed, max_speed);
      else
        ARA_LOGINFO(lg, "Speed={} (max={})", speed, max_speed);

      missed_ticks.store(0, std::memory_order_relaxed);
    }
  );

  // Simple watchdog: PHM alive + liveness
  using namespace std::chrono_literals;
  while (running.load()) {
    phm.ReportAlive();
    if (missed_ticks.fetch_add(1) > 30) { // ~3s if provider emits every 100ms
      ARA_LOGERROR(lg, "Missed speed events for >3s -> checkpoint");
      phm.ReportCheckpoint(0x1001);
      missed_ticks.store(0);
    }
    std::this_thread::sleep_for(100ms);
  }

  ARA_LOGINFO(lg, "Shutdown");
  return 0;
}
