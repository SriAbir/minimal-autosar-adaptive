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

// graceful shutdown flag
static std::atomic<bool> running{true};
static void on_sig(int) { running.store(false, std::memory_order_relaxed); }

int main() {
  std::signal(SIGINT,  on_sig);
  std::signal(SIGTERM, on_sig);

  // Transport-agnostic runtime backed by SOME/IP adapter
  ara::com::Runtime rt(ara::com::GetSomeipAdapter());

  // Logging
  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","sensor_provider");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("SNS");

  // PHM
  ara::phm::SupervisionClient phm("sensor_provider");
  phm.Connect();

  // Offer the Speed service via generic Skeleton
  ara::com::Skeleton<SpeedDesc> skel(rt, "sensor_provider");
  skel.Offer();

  using namespace std::chrono_literals;
  float t = 0.0f;

  while (running.load(std::memory_order_relaxed)) {
    phm.ReportAlive();

    float speed = 50.0f + 10.0f * std::sin(t);
    t += 0.2f;

    // Publish event (transport-agnostic). Codec<float> handles serialization.
    auto ec = skel.Notify<SpeedDesc::SpeedEvent>(speed);
    if (ec != ara::com::Errc::kOk) {
      ARA_LOGWARN(lg, "Notify failed with Errc={}", static_cast<int>(ec));
    } else {
      ARA_LOGDEBUG(lg, "Speed publish {}", speed);
    }

    std::this_thread::sleep_for(100ms);
  }

  skel.Stop();
  rt.adapter().shutdown();
  ARA_LOGINFO(lg, "Shutdown");
  return 0;
}
