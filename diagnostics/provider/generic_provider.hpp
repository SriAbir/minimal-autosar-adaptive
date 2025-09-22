#pragma once
#include "ifunction_diag_provider.hpp"
#include <functional>
#include <unordered_map>
#include <string>

namespace diag {

// Neutral app hooks (wired from your Function app)
struct VehicleFunctionApi {
  std::function<bool()> hasTrigger;            // Is a trigger currently active?
  std::function<std::string()> triggerCause;   // Free-form cause string
  std::function<void()> resetTrigger;          // Clear/acknowledge trigger
};

class GenericProvider : public IFunctionDiagProvider {
public:
  explicit GenericProvider(VehicleFunctionApi api);

  UdsResponse readDID(uint16_t did) override;
  UdsResponse readDTC(uint8_t subfunc) override;
  UdsResponse routineControl(uint8_t subfunc, uint16_t rid,
                             const std::vector<uint8_t>& payload) override;

private:
  VehicleFunctionApi api_;
  std::unordered_map<std::string, uint16_t> dids_;
  std::unordered_map<std::string, uint16_t> routines_;
  void loadConfig(const char* path);
};

} // namespace diag
