#pragma once
#include <cstdint>
#include <vector>
#include <optional>

namespace diag {

// Canonical UDS envelope types (FREE, not nested)
struct UdsRequest {
  uint8_t sid;
  std::vector<uint8_t> data;
};

struct UdsResponse {
  bool negative = false;     // true => 0x7F, else positive (sid + 0x40)
  uint8_t sid = 0;           // original SID (not +0x40)
  uint8_t nrc = 0;           // negative response code if negative==true
  std::vector<uint8_t> data; // positive payload
};

class IFunctionDiagProvider {
public:
  virtual ~IFunctionDiagProvider() = default;

  virtual UdsResponse readDID(uint16_t did) = 0;
  virtual UdsResponse readDTC(uint8_t subfunc) {
    return UdsResponse{true, 0x19, 0x12, {}}; // subFunction not supported
  }
  virtual UdsResponse routineControl(uint8_t subfunc, uint16_t rid,
                                     const std::vector<uint8_t>& payload) {
    (void)subfunc; (void)rid; (void)payload;
    return UdsResponse{true, 0x31, 0x12, {}};
  }

  virtual std::optional<UdsResponse> handleRaw(const UdsRequest&) { return std::nullopt; }
};

} // namespace diag
