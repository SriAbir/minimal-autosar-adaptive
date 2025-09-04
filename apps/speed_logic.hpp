#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <optional>
#include <cmath>
#include <per/key_value_storage.hpp>

// Just used for testing

namespace app {

struct SpeedEventResult {
  float speed{};
  bool  exceeded{};
};

// Parses a text payload (e.g., "72.3"). Non-numeric -> 0.0f
inline float ParseSpeedPayload(std::string_view s) {
  try {
    // trim spaces
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    if (b == std::string_view::npos) return 0.0f;
    std::string tmp{s.substr(b, e - b + 1)};
    return std::stof(tmp);
  } catch (...) {
    return 0.0f;
  }
}

// Handles one speed event: updates persistency and returns whether threshold exceeded
inline SpeedEventResult HandleSpeedEvent(ara::per::KeyValueStorage& kv,
                                         std::string_view payload_text,
                                         float max_allowed_speed) {
  float speed = ParseSpeedPayload(payload_text);
  kv.SetValue("last_speed", std::to_string(speed));
  return {speed, speed > max_allowed_speed};
}

} // namespace app
