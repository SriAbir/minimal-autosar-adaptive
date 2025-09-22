#pragma once
#include <functional>
#include <string>

namespace fn {

// Settable patterns/commands to be sent on CAN bus (examples that can be added to):
enum class LightPattern { Off=0, BlinkOnce=1, BlinkTwice=2, BlinkFast=3 };
enum class ActuatorCommand  { Neutral=0, ActionA=1, ActionB=2 };

// App → Gateway: the app emits events to be put on CAN. Examples:
struct FunctionToBus {
  std::function<void(LightPattern)> onLightPattern;
  std::function<void(ActuatorCommand)>  onActuator;
};

// Gateway → App: the gateway can feed line/sensor states back
struct BusToFunction {
  std::function<void(bool)> setLineA;   // e.g., a binary sensor
  std::function<void(bool)> setLineB;   // another binary
};

// Simple handle keept globally in apps to call gateway
struct GatewayHandle {
  std::function<void(LightPattern)> emitLight;
  std::function<void(ActuatorCommand)>  emitActuator;
};

} // namespace fn
