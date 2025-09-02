// include/log.hpp
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <utility>
#include <sstream>
#include <optional>

namespace ara::log {

// ---------- Log levels ----------
enum class LogLevel : uint8_t { kOff, kFatal, kError, kWarn, kInfo, kDebug, kVerbose };

inline constexpr std::string_view ToString(LogLevel lvl) {
  switch (lvl) {
    case LogLevel::kFatal:   return "FATAL";
    case LogLevel::kError:   return "ERROR";
    case LogLevel::kWarn:    return "WARN";
    case LogLevel::kInfo:    return "INFO";
    case LogLevel::kDebug:   return "DEBUG";
    case LogLevel::kVerbose: return "VERBOSE";
    default:                 return "OFF";
  }
}

// ---------- Record & sink ----------
struct LogRecord {
  // AUTOSAR-style tags
  std::string ecu_id;  // e.g., "ECU1"
  std::string app_id;  // e.g., "EMGR"
  std::string ctx_id;  // e.g., "SOME"
  LogLevel    level;
  std::string message;
  // wall-clock timestamp in ns since epoch
  uint64_t    ts_ns;
  const char* file = nullptr;
  uint32_t    line = 0;
};

struct ISink {
  virtual ~ISink() = default;
  virtual void write(const LogRecord& rec) noexcept = 0;
};

using SinkPtr = std::shared_ptr<ISink>;

// ---------- Manager (global config & sinks) ----------
class LogManager {
public:
  static LogManager& Instance() {
    static LogManager g;
    return g;
  }

  void SetGlobalIds(std::string ecu, std::string app) {
    std::scoped_lock lk(mu_);
    ecu_id_ = std::move(ecu);
    app_id_ = std::move(app);
  }

  // Optional: set default level for new loggers
  void SetDefaultLevel(LogLevel lvl) {
    std::scoped_lock lk(mu_);
    default_level_ = lvl;
  }

  void AddSink(SinkPtr s) {
    std::scoped_lock lk(mu_);
    sinks_.push_back(std::move(s));
  }

  // Snapshot sinks/ids for fast use in Logger
  void Snapshot(std::vector<SinkPtr>& out, std::string& ecu, std::string& app, LogLevel& def) const {
    std::scoped_lock lk(mu_);
    out = sinks_; ecu = ecu_id_; app = app_id_; def = default_level_;
  }

private:
  LogManager() = default;
  mutable std::mutex mu_;
  std::vector<SinkPtr> sinks_;
  std::string ecu_id_{"ECU"};
  std::string app_id_{"APP"};
  LogLevel default_level_{LogLevel::kInfo};
};

// ---------- Logger (per-context) ----------
class Logger {
public:
  // Create a context logger (ctxId like "EM", "SOME", ctxDesc not used here but handy for DLT registration in your DltSink)
  static Logger CreateLogger(std::string ctxId, std::string /*ctxDesc*/ = "", std::optional<LogLevel> level = std::nullopt) {
    std::vector<SinkPtr> sinks; std::string ecu, app; LogLevel def{};
    LogManager::Instance().Snapshot(sinks, ecu, app, def);
    Logger L(std::move(ctxId), std::move(ecu), std::move(app), sinks, level.value_or(def));
    return L;
  }

  LogLevel Level() const noexcept { return level_; }
  void SetLevel(LogLevel lvl) noexcept { level_ = lvl; }

  // Basic logging with preformatted message
  void Log(LogLevel lvl, std::string_view msg, const char* file = nullptr, uint32_t line = 0) {
    if (!ShouldLog(lvl)) return;
    LogRecord r;
    r.ecu_id = ecu_id_;
    r.app_id = app_id_;
    r.ctx_id = ctx_id_;
    r.level  = lvl;
    r.message = std::string(msg);
    r.file = file;
    r.line = line;
    r.ts_ns = NowNs();
    for (const auto& s : sinks_) if (s) s->write(r);
  }

  // Convenience helpers
  void Fatal (std::string_view m, const char* f=nullptr, uint32_t l=0){ Log(LogLevel::kFatal,   m,f,l); }
  void Error (std::string_view m, const char* f=nullptr, uint32_t l=0){ Log(LogLevel::kError,   m,f,l); }
  void Warn  (std::string_view m, const char* f=nullptr, uint32_t l=0){ Log(LogLevel::kWarn,    m,f,l); }
  void Info  (std::string_view m, const char* f=nullptr, uint32_t l=0){ Log(LogLevel::kInfo,    m,f,l); }
  void Debug (std::string_view m, const char* f=nullptr, uint32_t l=0){ Log(LogLevel::kDebug,   m,f,l); }
  void Verbose(std::string_view m,const char* f=nullptr, uint32_t l=0){ Log(LogLevel::kVerbose, m,f,l); }

  // Tiny formatting helper that doesn’t depend on fmt/std::format:
  template <typename... Args>
  void LogF(LogLevel lvl, const char* file, uint32_t line, std::string_view fmt, Args&&... args) {
    if (!ShouldLog(lvl)) return;
    std::ostringstream oss;
    FormatInto(oss, fmt, std::forward<Args>(args)...); //
    Log(lvl, oss.str(), file, line);
  }

  template <typename... Args> void FatalF (const char* f, uint32_t l, std::string_view fmt, Args&&... a){ LogF(LogLevel::kFatal,   f,l,fmt,std::forward<Args>(a)...); }
  template <typename... Args> void ErrorF (const char* f, uint32_t l, std::string_view fmt, Args&&... a){ LogF(LogLevel::kError,   f,l,fmt,std::forward<Args>(a)...); }
  template <typename... Args> void WarnF  (const char* f, uint32_t l, std::string_view fmt, Args&&... a){ LogF(LogLevel::kWarn,    f,l,fmt,std::forward<Args>(a)...); }
  template <typename... Args> void InfoF  (const char* f, uint32_t l, std::string_view fmt, Args&&... a){ LogF(LogLevel::kInfo,    f,l,fmt,std::forward<Args>(a)...); }
  template <typename... Args> void DebugF (const char* f, uint32_t l, std::string_view fmt, Args&&... a){ LogF(LogLevel::kDebug,   f,l,fmt,std::forward<Args>(a)...); }
  template <typename... Args> void VerboseF(const char* f, uint32_t l, std::string_view fmt, Args&&... a){ LogF(LogLevel::kVerbose, f,l,fmt,std::forward<Args>(a)...); }

  // Structured extension point (add key/value pairs later if you like)
  const std::string& ContextId() const noexcept { return ctx_id_; }

private:
  Logger(std::string ctx, std::string ecu, std::string app,
         std::vector<SinkPtr> sinks, LogLevel lvl)
      : ctx_id_(std::move(ctx)), ecu_id_(std::move(ecu)), app_id_(std::move(app)),
        sinks_(std::move(sinks)), level_(lvl) {}

  static uint64_t NowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
  }

  bool ShouldLog(LogLevel lvl) const noexcept {
    if (level_ == LogLevel::kOff) return false;
    // Order: FATAL(1) .. VERBOSE(6). Anything <= current level logs.
    return static_cast<uint8_t>(lvl) <= static_cast<uint8_t>(level_);
  }

  // Poor-man’s "{}" formatter: replaces each "{}" with next arg via operator<<
  static void ReplaceFirstBrace(std::ostringstream& oss, std::string_view& fmt) {
    auto pos = fmt.find("{}");
    if (pos == std::string_view::npos) { oss << fmt; fmt = {}; return; }
    oss << fmt.substr(0, pos);
    fmt.remove_prefix(pos + 2);
  }
  template <typename T, typename... Rest>
  static void FormatInto(std::ostringstream& oss, std::string_view fmt, T&& value, Rest&&... rest) {
    ReplaceFirstBrace(oss, fmt);
    oss << std::forward<T>(value);
    if constexpr (sizeof...(rest) == 0) { oss << fmt; }
    else { FormatInto(oss, fmt, std::forward<Rest>(rest)...); }
  }
  static void FormatInto(std::ostringstream& oss, std::string_view fmt) { oss << fmt; }

  std::string ctx_id_;
  std::string ecu_id_;
  std::string app_id_;
  std::vector<SinkPtr> sinks_;
  LogLevel level_;
};

// ---------- Convenience macros to capture file/line ----------
#define ARA_LOGFATAL(lg, fmt, ...)   (lg).FatalF(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define ARA_LOGERROR(lg, fmt, ...)   (lg).ErrorF(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define ARA_LOGWARN(lg,  fmt, ...)   (lg).WarnF (__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define ARA_LOGINFO(lg,  fmt, ...)   (lg).InfoF (__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define ARA_LOGDEBUG(lg, fmt, ...)   (lg).DebugF(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define ARA_LOGVERBOSE(lg, fmt, ...) (lg).VerboseF(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)

//Ola: Check if possible to use source info / src info type instead.

} // namespace ara::log
