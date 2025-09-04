#include <gtest/gtest.h>
#include <cmath>
#include "apps/sensor_logic.hpp"

TEST(SensorLogic, ComputesExpectedRange) {
  // Values should oscillate between 40 and 60
  for (int i = 0; i < 100; ++i) {
    float t = 0.1f * static_cast<float>(i);
    float s = app::ComputeSpeedFromPhase(t);
    EXPECT_GE(s, 40.0f);
    EXPECT_LE(s, 60.0f);
  }
}

TEST(SensorLogic, KnownPoints) {
  EXPECT_NEAR(app::ComputeSpeedFromPhase(0.0f), 50.0f, 1e-5f);
  EXPECT_NEAR(app::ComputeSpeedFromPhase(static_cast<float>(M_PI/2.0)), 60.0f, 1e-5f);
  EXPECT_NEAR(app::ComputeSpeedFromPhase(static_cast<float>(3.0*M_PI/2.0)), 40.0f, 1e-5f);
}
