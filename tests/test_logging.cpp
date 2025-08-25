#include <gtest/gtest.h>
#include "log.hpp"
#include <memory>
#include <vector>
#include <string>

using namespace ara::log;

// -------- Test helper sink that captures records --------
struct CaptureSink : ISink {
  std::vector<LogRecord> records;
  void write(const LogRecord& r) noexcept override { records.push_back(r); }
};

TEST(Logging, InfoMessageIsEmittedAtDefaultLevel) {
  auto sink = std::make_shared<CaptureSink>();
  LogManager::Instance().SetGlobalIds("ECU1", "APP1");
  LogManager::Instance().SetDefaultLevel(LogLevel::kInfo);
  LogManager::Instance().AddSink(sink);

  auto log = Logger::CreateLogger("EM", "Execution Manager");
  ARA_LOGINFO(log, "hello {}", 123);

  ASSERT_FALSE(sink->records.empty());
  const auto& r = sink->records.back();
  EXPECT_EQ(r.ecu_id, "ECU1");
  EXPECT_EQ(r.app_id, "APP1");
  EXPECT_EQ(r.ctx_id, "EM");
  EXPECT_EQ(std::string(ToString(r.level)), "INFO");
  EXPECT_NE(r.ts_ns, 0u);
  EXPECT_NE(r.file, nullptr);
  EXPECT_GT(r.line, 0u);
  EXPECT_NE(r.message.find("hello 123"), std::string::npos);
}

TEST(Logging, DebugIsFilteredWhenLevelInfo) {
  auto sink = std::make_shared<CaptureSink>();
  LogManager::Instance().SetGlobalIds("ECU1", "APP1");
  LogManager::Instance().SetDefaultLevel(LogLevel::kInfo);
  LogManager::Instance().AddSink(sink);

  auto log = Logger::CreateLogger("SOME", "SomeIP Shim");
  ARA_LOGDEBUG(log, "this should NOT appear {}", 42);

  EXPECT_TRUE(sink->records.empty()); // filtered out
}

TEST(Logging, PerContextLevelCanBeRaised) {
  auto sink = std::make_shared<CaptureSink>();
  LogManager::Instance().SetGlobalIds("ECU1", "APP1");
  LogManager::Instance().SetDefaultLevel(LogLevel::kInfo);
  LogManager::Instance().AddSink(sink);

  auto log = Logger::CreateLogger("SOME", "SomeIP Shim");
  log.SetLevel(LogLevel::kDebug);  // raise for this context only

  ARA_LOGDEBUG(log, "debug {}", 7);
  ASSERT_EQ(sink->records.size(), 1u);
  EXPECT_EQ(std::string(ToString(sink->records[0].level)), "DEBUG");
  EXPECT_NE(sink->records[0].message.find("debug 7"), std::string::npos);
}

TEST(Logging, BroadcastsToMultipleSinks) {
  struct CountSink : ISink { int n=0; void write(const LogRecord&) noexcept override { ++n; } };
  auto sinkA = std::make_shared<CountSink>();
  auto sinkB = std::make_shared<CountSink>();

  LogManager::Instance().SetGlobalIds("ECU1", "APP1");
  LogManager::Instance().SetDefaultLevel(LogLevel::kInfo);
  LogManager::Instance().AddSink(sinkA);
  LogManager::Instance().AddSink(sinkB);

  auto log = Logger::CreateLogger("EM");
  ARA_LOGINFO(log, "hi");

  EXPECT_EQ(sinkA->n, 1);
  EXPECT_EQ(sinkB->n, 1);
}

// --- DLT smoke test (auto-skip when not built with DLT) ---
#ifdef HAVE_DLT
  #include "sinks_dlt.hpp"
TEST(DLT, EmitsWithoutCrashWhenDaemonAbsent) {
  auto dlt = std::make_shared<DltSink>("TestApp");
  LogManager::Instance().SetGlobalIds("ECU1", "APP1");
  LogManager::Instance().SetDefaultLevel(LogLevel::kInfo);
  LogManager::Instance().AddSink(dlt);

  auto log = Logger::CreateLogger("EM", "Execution Manager");
  EXPECT_NO_THROW( ARA_LOGINFO(log, "dlt smoke {}", 1) );
}
#else
TEST(DLT, SkippedIfNotBuilt) {
  GTEST_SKIP() << "Built without DLT (HAVE_DLT not defined)";
}
#endif
