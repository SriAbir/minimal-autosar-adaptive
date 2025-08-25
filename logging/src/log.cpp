#include "log.hpp"
#include <iostream>
#include <memory>

using namespace ara::log;

struct ConsoleSink : ISink {
  void write(const LogRecord& r) noexcept override {
    std::cout << "[" << ToString(r.level) << "] "
              << r.ctx_id << ": " << r.message << std::endl;
  }
};

int main() {
  // Global setup
  LogManager::Instance().SetGlobalIds("ECU1", "EMGR");
  LogManager::Instance().SetDefaultLevel(LogLevel::kDebug);
  LogManager::Instance().AddSink(std::make_shared<ConsoleSink>()); // <-- important

  // Per-context logger
  auto log = Logger::CreateLogger("EM", "Execution Manager");
  ARA_LOGINFO(log, "Booting… version {} ({})", "0.1.0", 42);
  ARA_LOGDEBUG(log, "Spawned child pid={}", 1234);
}
