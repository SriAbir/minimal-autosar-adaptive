#include "sinks_dlt.hpp"
#include <cstring>
#include <iostream>

#ifdef HAVE_DLT
  #include <dlt/dlt_user.h>
#endif

namespace ara::log {

#ifdef HAVE_DLT
static DltLogLevelType to_dlt_level(LogLevel l) {
  switch (l) {
    case LogLevel::kFatal:   return DLT_LOG_FATAL;
    case LogLevel::kError:   return DLT_LOG_ERROR;
    case LogLevel::kWarn:    return DLT_LOG_WARN;
    case LogLevel::kInfo:    return DLT_LOG_INFO;
    case LogLevel::kDebug:   return DLT_LOG_DEBUG;
    case LogLevel::kVerbose: return DLT_LOG_VERBOSE;
    default:                 return DLT_LOG_INFO;
  }
}
#endif

DltSink::DltSink(std::string app_description)
  : app_desc_(std::move(app_description)) {}

DltSink::~DltSink() {
#ifdef HAVE_DLT
  // Optional: unregister contexts/app. Most setups let the OS clean this up.
#endif
}

void DltSink::ensureAppRegistered(const std::string& app_id) {
#ifdef HAVE_DLT
  if (registered_app_id_ == app_id) return;
  // Register new app
  dlt_register_app(app_id.c_str(), app_desc_.c_str());
  registered_app_id_ = app_id;
#else
  (void)app_id; // unused
#endif
}

void DltSink::ensureCtxRegistered(const std::string& /*app_id*/,
                                  const std::string& ctx_id,
                                  const std::string& ctx_desc) {
#ifdef HAVE_DLT
  if (ctx_by_id_.find(ctx_id) != ctx_by_id_.end()) return;
  DltContext* ctx = new DltContext(); // freed on process exit
  std::memset(ctx, 0, sizeof(DltContext));
  dlt_register_context(ctx, ctx_id.c_str(), ctx_desc.c_str());
  ctx_by_id_[ctx_id] = CtxHandle{ctx};
#else
  (void)ctx_id; (void)ctx_desc;
#endif
}

void DltSink::write(const LogRecord& r) noexcept {
#ifdef HAVE_DLT
  std::scoped_lock lk(mu_);
  ensureAppRegistered(r.app_id);
  // You may want a nicer description per context; we use ctx_id as desc for now.
  ensureCtxRegistered(r.app_id, r.ctx_id, r.ctx_id);

  auto it = ctx_by_id_.find(r.ctx_id);
  if (it == ctx_by_id_.end() || it->second.h == nullptr) return;
  auto* ctx = static_cast<DltContext*>(it->second.h);

  // You can add file/line as separate args if you like.
  DLT_LOG(*ctx, to_dlt_level(r.level), DLT_STRING(r.message.c_str()));
#else
  // If built without DLT, warn once to stderr (optional).
  static bool warned = false;
  if (!warned) {
    std::cerr << "[DLT] Built without DLT support (HAVE_DLT not defined). "
                 "Install libdlt-dev and rebuild with -DHAVE_DLT -ldlt\n";
    warned = true;
  }
  (void)r;
#endif
}

} // namespace ara::log
