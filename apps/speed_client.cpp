#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <thread>
#include <string>

#include "log.hpp"
#include "sinks_console.hpp"

#include "ara/com/core.hpp"
#include "ara/com/someip_adapter.hpp"
#include "services_description.hpp"          
#include <ara/phm/supervision_client.hpp>
#include "persistency/key_value_storage_backend.hpp" 
#include "ara/per/key_value_storage.hpp"


static std::atomic<bool> running{true};
static void on_sig(int){ running=false; }

int main() {
  std::signal(SIGTERM, on_sig);

  // Transport-agnostic runtime backed by SOME/IP adapter
  ara::com::Runtime rt(ara::com::GetSomeipAdapter());

  // logging
  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","speed_client");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("SPD");

  // persistency
  auto backend = std::make_shared<::persistency::KeyValueStorageBackend>("/var/adaptive/per/demo");
  ara::per::KeyValueStorage kv(backend);
  float max_speed = 90.0f;
  if (auto r = kv.GetValue<float>("max_allowed_speed"); r.HasValue()) max_speed = r.Value();
  else kv.SetValue("max_allowed_speed", std::to_string(max_speed));

  // PHM
  ara::phm::SupervisionClient phm("speed_client");
  phm.Connect();

  // ---- ara::com-style client ----
  ara::com::Proxy<SpeedDesc> proxy(rt, "speed_client");
  if (!proxy.RequestService()) {
    ARA_LOGWARN(lg, "Speed service not available yet; will wait and keep PHM alive.");
  }

  std::atomic<int> missed_ticks{0};

  // Subscribe to the speed event (transport-agnostic)
  auto sub = proxy.Subscribe<SpeedDesc::SpeedEvent>(
    [&](float speed){
      kv.SetValue("last_speed", std::to_string(speed));
      if (speed > max_speed)
        ARA_LOGWARN(lg, "Speed {} exceeds threshold {}!", speed, max_speed);
      else
        ARA_LOGINFO(lg, "Speed={} (max={})", speed, max_speed);
      missed_ticks.store(0, std::memory_order_relaxed);
    }
  );
  
  using namespace std::chrono_literals;
  while (running.load(std::memory_order_relaxed)) {
    phm.ReportAlive();
    if (missed_ticks.fetch_add(1, std::memory_order_relaxed) > 30) { // ~3s if provider emits every 100ms
      ARA_LOGERROR(lg, "Missed speed events for >3s -> checkpoint");
      phm.ReportCheckpoint(0x1001);
      missed_ticks.store(0, std::memory_order_relaxed);
    }
    std::this_thread::sleep_for(100ms);
  }

  // Clean up (optional but nice if your adapter/binding supports it)
  rt.adapter().unsubscribe_event(sub);
  proxy.ReleaseService();
  rt.adapter().shutdown();
  ARA_LOGINFO(lg, "Shutdown");
  return 0;
}
