#pragma once
//
// hal/timing.h — thin wrapper over Arduino's micros(). Header-only since it
// is a single trivial passthrough; a .cpp would add nothing.
//
// The scheduler (docs/scheduler.md) and every timestamped struct in
// core/types.h are built around micros(), not millis() — microsecond
// resolution matters at a 1 kHz tick.

#include <Arduino.h>
#include <cstdint>

namespace revali {
namespace hal {

inline uint32_t now_us() {
    return micros();
}

}  // namespace hal
}  // namespace revali
