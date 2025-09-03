#pragma once
#include <cstdint>

namespace phm_ids {
    constexpr std::uint16_t kService   = 0x7A01;
    constexpr std::uint16_t kInstance  = 0x0001;
    constexpr std::uint16_t kAlive     = 0x0001; // ReportAlive()
    constexpr std::uint16_t kCheckpoint= 0x0002; // ReportCheckpoint(uint32)
}