#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <thread>
#include <string>
#include "log.hpp"
#include "sinks_console.hpp"
#include "com/someip_binding.hpp"
#include <ara/phm/supervision_client.hpp>

namespace demo_if {
  constexpr std::uint16_t kServiceId   = 0x1234;
  constexpr std::uint16_t kInstanceId  = 0x0001;
  constexpr std::uint16_t kEventId     = 0x8001;
  constexpr std::uint16_t kEventGroup  = 0x0001;
}

static std::atomic<bool> running{true};
static void on_sig(int){ running=false; }

int main() {
  std::signal(SIGTERM, on_sig);

  auto &LM = ara::log::LogManager::Instance();
  LM.SetGlobalIds("ECU1","sensor_provider");
  LM.SetDefaultLevel(ara::log::LogLevel::kInfo);
  LM.AddSink(std::make_shared<ara::log::ConsoleSink>());
  auto lg = ara::log::Logger::CreateLogger("SNS");

  ara::phm::SupervisionClient phm("sensor_provider");

  someip::init("sensor_provider");
  // Offer service + event (your wrapper also subscribes provider to EG; OK)
  someip::offer_service(demo_if::kServiceId, demo_if::kInstanceId,
                        demo_if::kEventId, demo_if::kEventGroup);

  using namespace std::chrono_literals;
  float t = 0.f;
  while (running.load()) {
    phm.ReportAlive();

    float speed = 50.f + 10.f * std::sin(t);
    t += 0.2f;

    // Your wrapper uses std::string payloads. Send ASCII float:
    someip::send_notification(demo_if::kServiceId, demo_if::kInstanceId,
                              demo_if::kEventId, std::to_string(speed));

    ARA_LOGDEBUG(lg, "Speed publish {}", speed);
    std::this_thread::sleep_for(100ms);
  }

  ARA_LOGINFO(lg, "Shutdown");
  return 0;
}
