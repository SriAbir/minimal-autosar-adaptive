#include <gtest/gtest.h>

#include <persistency/storage_registry.hpp>
#include <ara/per/key_value_storage.hpp>
#include <ara/per/file_storage.hpp>

#include <string>
#include <vector>

#ifndef K_MANIFEST_PATH
#  define K_MANIFEST_PATH "manifests/persistency.json"
#endif

using persistency::StorageRegistry;

// Common: init registry once
class PersistencyBase : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    auto r = StorageRegistry::Instance().InitFromFile(K_MANIFEST_PATH);
    ASSERT_TRUE(r.HasValue()) << "Failed to load " << K_MANIFEST_PATH;
    ASSERT_TRUE(StorageRegistry::Instance().IsInitialized());
  }
};

// KV-only fixture
class PersistencyKV : public PersistencyBase {
  void SetUp() override {
    
    auto cfg = StorageRegistry::Instance().Lookup("EM/KV/Settings");
    ASSERT_TRUE(cfg.has_value()) << "No registry entry for EM/KV/Settings";
    std::cerr << "[TEST] KV base = " << cfg->base_path
              << " quota=" << cfg->quota_bytes << "\n";
    std::cerr << "[TEST] before ResetKeyValueStorage\n";
    auto r = ara::per::ResetKeyValueStorage(ara::core::InstanceSpecifier{"EM/KV/Settings"});
    std::cerr << "[TEST] after ResetKeyValueStorage: ok=" << r.HasValue() << "\n";
    ASSERT_TRUE(r.HasValue());
  }
};

TEST_F(PersistencyKV, KeyValue_BasicSetGetRemove) {
  using namespace ara::per;
  SCOPED_TRACE("OpenKeyValueStorage");
  std::cerr << "[TEST] before OpenKeyValueStorage\n";
  auto h = OpenKeyValueStorage(ara::core::InstanceSpecifier{"EM/KV/Settings"});
  std::cerr << "[TEST] after OpenKeyValueStorage: ok=" << h.HasValue() << "\n";
  ASSERT_TRUE(h.HasValue());
  auto kv = h.Value();

  SCOPED_TRACE("SetValue");
  std::cerr << "[TEST] before SetValue\n";
  ASSERT_TRUE(kv->SetValue("foo", std::string("bar")).HasValue());
  std::cerr << "[TEST] after SetValue\n";

  SCOPED_TRACE("GetValue");
  auto get = kv->GetValue<std::string>("foo");
  ASSERT_TRUE(get.HasValue());
  EXPECT_EQ(get.Value(), "bar");

  SCOPED_TRACE("RemoveKey");
  ASSERT_TRUE(kv->RemoveKey("foo").HasValue());
}

// FS-only fixture
class PersistencyFS : public PersistencyBase {
  void SetUp() override {

    auto r = ara::per::ResetFileStorage(ara::core::InstanceSpecifier{"EM/FS/State"});
    ASSERT_TRUE(r.HasValue());
  }
};

TEST_F(PersistencyFS, File_BasicWriteReadRemove) {
  using namespace ara::per;
  auto h = OpenFileStorage(ara::core::InstanceSpecifier{"EM/FS/State"}, 0);
  ASSERT_TRUE(h.HasValue());
  auto fs = h.Value();

  const std::vector<uint8_t> data{1,2,3,4,5};
  ASSERT_TRUE(fs->WriteFile("test.bin", data).HasValue());
  auto r = fs->ReadFile("test.bin");
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), data);
  ASSERT_TRUE(fs->RemoveFile("test.bin").HasValue());
}
