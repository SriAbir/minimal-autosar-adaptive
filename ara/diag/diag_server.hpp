// ara/diag/diag_server.hpp  (public-safe)
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include <utility>

namespace ara::diag {

enum class Nrc : uint8_t { kOk=0x00, kSubFuncNotSupported=0x12, kOutOfRange=0x31, kInvalidLen=0x13 };

struct RdbiHandler {
  // Return {nrc!=Ok => negative} else positive payload
  std::function<std::pair<Nrc,std::vector<uint8_t>>(uint16_t did)> onRead;
};

struct RtcHandler {
  // sub=0x01 start, 0x02 stop, 0x03 reset (subset)
  std::function<Nrc(uint8_t sub, uint16_t rid, const std::vector<uint8_t>& payload)> onRoutine;
};

class DiagServer {
    public:
    // Perhaps we'll add Session/Security later; for now just handlers
    void RegisterRdbi(RdbiHandler h) { rdbi_ = std::move(h); }
    void RegisterRoutine(RtcHandler h){ rtc_  = std::move(h); }

    // Start a tiny TCP UDS loop internally
    // bind="127.0.0.1", port=derived or DIAG_PORT
    void Run(const char* bind_addr, uint16_t port);

    // Internal dispatch used by the transport:
    std::pair<Nrc,std::vector<uint8_t>> HandleRdbi(uint16_t did) {
        if (!rdbi_.onRead) return {Nrc::kOutOfRange, {}};
        return rdbi_.onRead(did);
    }
    Nrc HandleRoutine(uint8_t sub, uint16_t rid, const std::vector<uint8_t>& pl) {
        if (!rtc_.onRoutine) return Nrc::kSubFuncNotSupported;
        return rtc_.onRoutine(sub, rid, pl);
    }
    private:
    RdbiHandler rdbi_{};
    RtcHandler  rtc_{};
    };

} // namespace ara::diag
