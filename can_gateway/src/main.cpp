// FOR STAND ALONE TESTING OF CAN GATEWAY

#include "function_bus_api.hpp"
#include <iostream>
#include <thread>
#include <chrono>

extern "C" fn::GatewayHandle make_can_gateway(const char* cfg_path, fn::BusToFunction bus2fn);

int main(int argc, char** argv) {
  const char* cfg = (argc > 1) ? argv[1] : "can_gateway/config/can-example.json";

  fn::BusToFunction b2f {
    /*setLineA*/ [](bool v){ std::cout << "[RX] LineA=" << v << "\n"; },
    /*setLineB*/ [](bool v){ std::cout << "[RX] LineB=" << v << "\n"; }
  };

  auto handle = make_can_gateway(cfg, b2f);

  // Demo TX: emit generic patterns/commands
  if (handle.emitLight) handle.emitLight(fn::LightPattern::BlinkTwice);
  if (handle.emitActuator)  handle.emitActuator(fn::ActuatorCommand::ActionA);

  // Keep alive to receive frames
  std::this_thread::sleep_for(std::chrono::seconds(5));
  return 0;
}
