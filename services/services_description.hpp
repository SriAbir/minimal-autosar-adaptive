// services/services_desc.hpp — small & reusable
#pragma once
#include "ara/com/core.hpp"
#include <functional>

struct SpeedDesc {
  static constexpr ara::com::ServiceId  kServiceId       = 0x1234;
  static constexpr ara::com::InstanceId kInstanceId      = 0x0001;
  static constexpr const char*          kDefaultClient   = "speed_client";
  static constexpr const char*          kDefaultServer   = "sensor_provider";

  struct SpeedEvent {
    using Payload                 = float;
    using Callback                = std::function<void(float)>;
    static constexpr ara::com::EventId      kId    = 0x8001;
    static constexpr ara::com::EventGroupId kGroup = 0x0001;
  };

  // Methods could be added similarly:
  // struct SetMaxSpeed { using Request=float; using Response=void; static constexpr MethodId kId = 0x0002; };
};
