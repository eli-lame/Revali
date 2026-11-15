#pragma once
//
// board/pins.h — the single source of truth for vehicle GPIO assignment.
//
// This MUST match docs/hardware.md "Pin map — vehicle ESP32" exactly. If you
// change a pin here, update that table too, and vice versa — the two are not
// allowed to drift apart. See docs/current_devtasks.md task [1.1].

#include <cstdint>

namespace revali {
namespace board {

// ---------------------------------------------------------------------------
// IMU — SPI (VSPI). See docs/hardware.md "Put the IMU on SPI, not I2C".
// ---------------------------------------------------------------------------
inline constexpr uint8_t kImuSclk = 23;
inline constexpr uint8_t kImuMiso = 25;
inline constexpr uint8_t kImuMosi = 19;
inline constexpr uint8_t kImuCs = 15;
inline constexpr uint8_t kImuInt = 35;  // data-ready interrupt — use it, don't poll

// ---------------------------------------------------------------------------
// ToF pair — I2C, shared bus. See docs/hardware.md "Both VL53L0X sensors
// boot at address 0x29" for why XSHUT is per-sensor and not tied together.
// ---------------------------------------------------------------------------
inline constexpr uint8_t kI2cSda = 22;
inline constexpr uint8_t kI2cScl = 21;
inline constexpr uint32_t kI2cClockHz = 400000;
inline constexpr uint8_t kTofAXshut = 18;  // front-left
inline constexpr uint8_t kTofBXshut = 26;  // rear-right

// ---------------------------------------------------------------------------
// Motors — order matches the mixer's front-left/front-right/rear-right/
// rear-left convention in docs/control_loop.md. Do not reorder these without
// also updating the mixer, or roll/pitch will be driven backwards.
// ---------------------------------------------------------------------------
inline constexpr uint8_t kMotorFrontLeft = 32;
inline constexpr uint8_t kMotorFrontRight = 33;
inline constexpr uint8_t kMotorRearRight = 27;
inline constexpr uint8_t kMotorRearLeft = 14;

// ---------------------------------------------------------------------------
// Battery sense — divider midpoint. ADC1 only; ADC2 is unusable once the
// radio is active. See docs/hardware.md "Battery sense wiring".
// ---------------------------------------------------------------------------
inline constexpr uint8_t kBatterySense = 34;

// ---------------------------------------------------------------------------
// Status / feedback
// ---------------------------------------------------------------------------
inline constexpr uint8_t kStatusLed = 2;      // onboard LED, no external wiring
inline constexpr uint8_t kArmingBuzzer = 13;  // optional

// Avoid: GPIO 6-11 (flash). Be careful with 0, 2, 12, 15 (strapping) — a
// pull-up on GPIO 12 at boot bricks the boot voltage selection.

}  // namespace board
}  // namespace revali
