#pragma once
#include "log.hpp"
#include <iostream>

namespace ara::log {

struct ConsoleSink : ISink {
  void write(const LogRecord& r) noexcept override {
    std::cout << "[" << ToString(r.level) << "] "
              << r.ctx_id << ": " << r.message << std::endl;
  }
};

} // namespace ara::log
