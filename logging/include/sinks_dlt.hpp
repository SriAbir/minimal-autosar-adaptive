#pragma once
#include "log.hpp"
#include <mutex>
#include <unordered_map>
#include <string>

namespace ara::log {

class DltSink : public ISink {
public:
  // You can pass a default app description shown in tools; optional.
  explicit DltSink(std::string app_description = "AdaptiveApp");
  ~DltSink();

  void write(const LogRecord& r) noexcept override;

private:
  void ensureAppRegistered(const std::string& app_id);
  void ensureCtxRegistered(const std::string& app_id,
                           const std::string& ctx_id,
                           const std::string& ctx_desc);

  // Map ctx_id -> DLT context handle
  struct CtxHandle { void* h = nullptr; }; // opaque to avoid including dlt headers here
  std::mutex mu_;
  std::string app_desc_;
  std::string registered_app_id_;
  std::unordered_map<std::string, CtxHandle> ctx_by_id_;
};

} // namespace ara::log
