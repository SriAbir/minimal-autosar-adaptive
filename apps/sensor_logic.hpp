#pragma once
#include <cmath>

//Just used for testing

namespace app {

// Speed model used by the provider (kept here so we can test it)
inline float ComputeSpeedFromPhase(float t) {
  return 50.0f + 10.0f * std::sin(t);
}

} // namespace app
