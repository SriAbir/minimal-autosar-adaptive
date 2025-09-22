#include "function_bus_api.hpp"
#include <nlohmann/json.hpp>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <optional>
#include <cstring>
#include <cstdlib>

using nlohmann::json;

namespace {

struct TxMap {
  uint32_t ind_canid = 0;
  uint32_t act_canid = 0;
  uint8_t  ind_dlc   = 1;
  uint8_t  act_dlc   = 1;
};

struct RxMap {
  std::optional<uint32_t> lineA_id;
  int lineA_bit = 0;
  std::optional<uint32_t> lineB_id;
  int lineB_bit = 1;
};

struct Cfg {
  std::string iface = "vcan0";
  TxMap tx;
  RxMap rx;
};

std::optional<Cfg> load_cfg(const char* path) {
  std::ifstream f(path);
  if (!f) { std::cerr << "CAN cfg not found: " << path << "\n"; return std::nullopt; }
  json j; f >> j;
  Cfg c;
  if (j.contains("iface")) c.iface = j["iface"].get<std::string>();
  c.tx.ind_canid = j["tx"]["LightPattern"]["can_id"].get<uint32_t>();
  c.tx.ind_dlc   = j["tx"]["LightPattern"]["dlc"].get<uint8_t>();
  c.tx.act_canid = j["tx"]["ActuatorCommand"]["can_id"].get<uint32_t>();
  c.tx.act_dlc   = j["tx"]["ActuatorCommand"]["dlc"].get<uint8_t>();

  if (j.contains("rx") && j["rx"].contains("LineA")) {
    c.rx.lineA_id = j["rx"]["LineA"]["can_id"].get<uint32_t>();
    c.rx.lineA_bit = j["rx"]["LineA"]["bit"].get<int>();
  }
  if (j.contains("rx") && j["rx"].contains("LineB")) {
    c.rx.lineB_id = j["rx"]["LineB"]["can_id"].get<uint32_t>();
    c.rx.lineB_bit = j["rx"]["LineB"]["bit"].get<int>();
  }
  return c;
}

int open_can(const std::string& iface) {
  int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (s < 0) { perror("socket(PF_CAN)"); return -1; }
  sockaddr_can addr{};
  ifreq ifr{};
  std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface.c_str());
  if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror("SIOCGIFINDEX"); close(s); return -1; }
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind(PF_CAN)"); close(s); return -1; }
  return s;
}

} // namespace

// Public gateway “engine”
class GenericCanGateway {
public:
  GenericCanGateway(const Cfg& cfg, fn::BusToFunction bus2fn)
  : cfg_(cfg), bus2fn_(bus2fn) {
    sock_ = open_can(cfg_.iface);
    running_ = (sock_ >= 0);
    if (running_) rx_thread_ = std::thread([this]{ rx_loop(); });
  }

  ~GenericCanGateway() {
    running_ = false;
    if (rx_thread_.joinable()) rx_thread_.join();
    if (sock_ >= 0) close(sock_);
  }

  void sendIndicator(fn::LightPattern p) {
    if (sock_ < 0) return;
    can_frame f{};
    f.can_id = cfg_.tx.ind_canid;
    f.can_dlc = cfg_.tx.ind_dlc;
    f.data[0] = static_cast<uint8_t>(p);
    (void)write(sock_, &f, sizeof(f));
  }

  void sendActuator(fn::ActuatorCommand c) {
    if (sock_ < 0) return;
    can_frame f{};
    f.can_id = cfg_.tx.act_canid;
    f.can_dlc = cfg_.tx.act_dlc;
    f.data[0] = static_cast<uint8_t>(c);
    (void)write(sock_, &f, sizeof(f));
  }

private:
  void rx_loop() {
    while (running_) {
      can_frame f{};
      ssize_t n = read(sock_, &f, sizeof(f));
      if (n != sizeof(f)) continue;

      if (cfg_.rx.lineA_id && f.can_id == *cfg_.rx.lineA_id) {
        bool v = (f.data[0] >> cfg_.rx.lineA_bit) & 0x1;
        if (bus2fn_.setLineA) bus2fn_.setLineA(v);
      }
      if (cfg_.rx.lineB_id && f.can_id == *cfg_.rx.lineB_id) {
        bool v = (f.data[0] >> cfg_.rx.lineB_bit) & 0x1;
        if (bus2fn_.setLineB) bus2fn_.setLineB(v);
      }
    }
  }

  Cfg cfg_;
  fn::BusToFunction bus2fn_;
  int sock_ = -1;
  std::atomic<bool> running_{false};
  std::thread rx_thread_;
};

// Factory to be used by your app
extern "C" fn::GatewayHandle make_can_gateway(const char* cfg_path,
                                              fn::BusToFunction bus2fn) {
  static std::unique_ptr<GenericCanGateway> gw;
  auto cfg = load_cfg(cfg_path ? cfg_path : "can_gateway/config/can-example.json");
  if (!cfg) {
    std::cerr << "CAN gateway: using defaults vcan0 with no TX/RX\n";
    return {};
  }
  gw = std::make_unique<GenericCanGateway>(*cfg, bus2fn);
  fn::GatewayHandle h;
  h.emitLight = [=](fn::LightPattern p){ gw->sendIndicator(p); };
  h.emitActuator  = [=](fn::ActuatorCommand c){ gw->sendActuator(c);  };
  return h;
}
