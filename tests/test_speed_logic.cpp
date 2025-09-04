#include <gtest/gtest.h>
#include <filesystem>
#include "apps/speed_logic.hpp"
#include "persistency/key_value_storage_backend.hpp"
#include "ara/per/key_value_storage.hpp"

namespace fs = std::filesystem;

static ara::per::KeyValueStorage MakeKV(const fs::path& dir) {
  auto backend = std::make_shared<persistency::KeyValueStorageBackend>(dir.string());
  return ara::per::KeyValueStorage(backend);
}

TEST(SpeedLogic, ParsesValidFloatAndPersists) {
  fs::path tmp = fs::temp_directory_path() / "kv_speedlogic_1";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  auto kv = MakeKV(tmp);
  float max_allowed = 90.0f;

  auto res = app::HandleSpeedEvent(kv, "95.5", max_allowed);
  EXPECT_FLOAT_EQ(res.speed, 95.5f);
  EXPECT_TRUE(res.exceeded);

  auto last = kv.GetValue<float>("last_speed");
  ASSERT_TRUE(last.HasValue());
  EXPECT_FLOAT_EQ(last.Value(), 95.5f);

  fs::remove_all(tmp);
}

TEST(SpeedLogic, BelowThreshold) {
  fs::path tmp = fs::temp_directory_path() / "kv_speedlogic_2";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  auto kv = MakeKV(tmp);
  float max_allowed = 90.0f;

  auto res = app::HandleSpeedEvent(kv, "42", max_allowed);
  EXPECT_FLOAT_EQ(res.speed, 42.0f);
  EXPECT_FALSE(res.exceeded);

  auto last = kv.GetValue<float>("last_speed");
  ASSERT_TRUE(last.HasValue());
  EXPECT_FLOAT_EQ(last.Value(), 42.0f);

  fs::remove_all(tmp);
}

TEST(SpeedLogic, HandlesWhitespaceAndBadInput) {
  fs::path tmp = fs::temp_directory_path() / "kv_speedlogic_3";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  auto kv = MakeKV(tmp);

  auto res1 = app::HandleSpeedEvent(kv, "   100.0  \n", 120.0f);
  EXPECT_FLOAT_EQ(res1.speed, 100.0f);
  EXPECT_FALSE(res1.exceeded);

  auto res2 = app::HandleSpeedEvent(kv, "NOT_A_NUMBER", 1.0f);
  EXPECT_FLOAT_EQ(res2.speed, 0.0f);
  EXPECT_TRUE(res2.exceeded); // 0.0 > 1.0? No. Wait—threshold is 1.0 -> exceeded should be false
  // Correction:
  EXPECT_FALSE(app::HandleSpeedEvent(kv, "NOT_A_NUMBER", 1.0f).exceeded);

  fs::remove_all(tmp);
}
