// ara/diag/diag_server.cpp  (self-contained)
#include "ara/diag/diag_server.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstdio>   // for perror

namespace {
// encode UDS replies as bytes -> std::string
inline std::string uds_pos(uint8_t sid, const std::vector<uint8_t>& payload){
  std::string out; out.reserve(1 + payload.size());
  out.push_back(static_cast<char>(sid + 0x40));
  if (!payload.empty())
    out.append(reinterpret_cast<const char*>(payload.data()), payload.size());
  return out;
}
inline std::string uds_neg(uint8_t sid, uint8_t nrc){
  std::string out; out.resize(3);
  out[0] = static_cast<char>(0x7F);
  out[1] = static_cast<char>(sid);
  out[2] = static_cast<char>(nrc);
  return out;
}
inline uint16_t pick_port(const char* name){
  unsigned h=0; for (auto*p=name; p && *p; ++p) h = h*131u + (unsigned)*p;
  return static_cast<uint16_t>(13400 + (h % 100));
}
} // namespace

namespace ara::diag {

void DiagServer::Run(const char* bind_addr, uint16_t port) {
  std::string b = bind_addr ? bind_addr : "127.0.0.1";
  uint16_t p = port ? port : pick_port("app");

  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { std::perror("socket"); return; }
  int opt = 1; ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(p);
  if (::inet_pton(AF_INET, b.c_str(), &addr.sin_addr) != 1) {
    std::perror("inet_pton"); ::close(s); return;
  }
  if (::bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) { std::perror("bind"); ::close(s); return; }
  if (::listen(s, 4) < 0) { std::perror("listen"); ::close(s); return; }

  // accept-loop: each connection carries one [u16 length][payload] UDS APDU
  while (true) {
    int c = ::accept(s, nullptr, nullptr);
    if (c < 0) { std::perror("accept"); continue; }

    unsigned char hdr[2];
    if (::read(c, hdr, 2) != 2) { ::close(c); continue; }
    uint16_t len = static_cast<uint16_t>((hdr[0] << 8) | hdr[1]);

    std::string req(len, '\0');
    if (::read(c, req.data(), len) != len) { ::close(c); continue; }
    if (req.empty()) { ::close(c); continue; }

    const uint8_t sid = static_cast<uint8_t>(req[0]);
    std::vector<uint8_t> data(req.begin()+1, req.end());
    std::string rsp;

    auto neg = [&](uint8_t s, Nrc n){ return uds_neg(s, static_cast<uint8_t>(n)); };

    switch (sid) {
      case 0x22: { // ReadDataByIdentifier
        if (data.size() < 2) { rsp = neg(0x22, Nrc::kInvalidLen); break; }
        uint16_t did = static_cast<uint16_t>((data[0] << 8) | data[1]);
        auto [nrc, payload] = HandleRdbi(did);
        rsp = (nrc==Nrc::kOk) ? uds_pos(0x22, payload) : neg(0x22, nrc);
        break;
      }
      case 0x31: { // RoutineControl
        if (data.size() < 3) { rsp = neg(0x31, Nrc::kInvalidLen); break; }
        uint8_t  sub = data[0];
        uint16_t rid = static_cast<uint16_t>((data[1] << 8) | data[2]);
        std::vector<uint8_t> pl(data.begin()+3, data.end());
        auto nrc = HandleRoutine(sub, rid, pl);
        rsp = (nrc==Nrc::kOk) ? uds_pos(0x31, {}) : neg(0x31, nrc);
        break;
      }
      case 0x19: { // ReadDTCInformation (template: no DTCs)
        rsp = uds_pos(0x19, {});
        break;
      }
      default:
        rsp = neg(sid, Nrc::kOutOfRange);
    }

    uint16_t rlen = static_cast<uint16_t>(rsp.size());
    unsigned char ohdr[2] = {
      static_cast<unsigned char>(rlen >> 8),
      static_cast<unsigned char>(rlen & 0xFF)
    };
    ::write(c, ohdr, 2);
    if (rlen) ::write(c, rsp.data(), rlen);
    ::close(c);
  }
}

} // namespace ara::diag
