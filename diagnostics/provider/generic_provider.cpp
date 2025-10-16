#include "generic_provider.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>

using nlohmann::json;
namespace diag {

UdsResponse GenericProvider::readDID(uint16_t did) {
  if (did == dids_["HasTrigger"]) {
    uint8_t v = api_.hasTrigger ? (api_.hasTrigger() ? 1 : 0) : 0;
    return UdsResponse{false, 0x22, 0x00, std::vector<uint8_t>{v}};
  }
  if (did == dids_["TriggerCause"]) {
    std::string s = api_.triggerCause ? api_.triggerCause() : std::string("None");
    return UdsResponse{false, 0x22, 0x00, std::vector<uint8_t>(s.begin(), s.end())};
  }
  return UdsResponse{true, 0x22, 0x31, {}}; // 0x31: requestOutOfRange
}

UdsResponse GenericProvider::readDTC(uint8_t subfunc) {
  (void)subfunc;
  return UdsResponse{false, 0x19, 0x00, {}}; // “no DTCs” template
}

UdsResponse GenericProvider::routineControl(uint8_t subfunc, uint16_t rid,
                                            const std::vector<uint8_t>& payload) {
  (void)payload;
  if (subfunc == 0x01 /*start*/ && rid == routines_["ResetTrigger"]) {
    if (api_.resetTrigger) api_.resetTrigger();
    return UdsResponse{false, 0x31, 0x00, {}};
  }
  return UdsResponse{true, 0x31, 0x12, {}}; // subFunction not supported
}

} // namespace diag
