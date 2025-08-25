#include "log.hpp"
#include "sinks_console.hpp"
#include "sinks_dlt.hpp"

using namespace ara::log;

int main() {
  // Global setup
  LogManager::Instance().SetGlobalIds("ECU1", "EMGR");
  LogManager::Instance().SetDefaultLevel(LogLevel::kDebug);

  // Add sinks
  LogManager::Instance().AddSink(std::make_shared<ConsoleSink>());
  LogManager::Instance().AddSink(std::make_shared<DltSink>("Execution Manager"));

  // Per-context logger
  auto log = Logger::CreateLogger("EM", "Execution Manager");
  ARA_LOGINFO(log,  "Booting… version {} ({})", "0.1.0", 42);
  ARA_LOGDEBUG(log, "Spawned child pid={}", 1234);
}
