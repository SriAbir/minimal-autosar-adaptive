#pragma once
#include <cstdint>

namespace demo_if {
constexpr std::uint16_t kServiceId   = 0x1234;
constexpr std::uint16_t kInstanceId  = 0x0001;

// Methods
constexpr std::uint16_t kGetAverageSpeed_MethodId = 0x4001;

// Events
constexpr std::uint16_t kSpeedKmH_EventId = 0x8001;

// Simple wire format for SpeedKmH: 32-bit little-endian float (km/h)
}
