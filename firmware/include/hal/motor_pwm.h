#pragma once
//
// hal/motor_pwm.h — thin wrapper over the ESP32 LEDC peripheral, generating
// standard hobby ESC PWM (50 Hz, 1000-2000 us pulse width).
//
// ASSUMPTION, FLAGGED: this targets standard analog PWM ESCs. If the ESCs
// actually on hand speak a digital protocol (DShot, Multishot, etc.), this
// HAL is the wrong shape entirely — those need an RMT-based bit-banged
// implementation, not LEDC PWM. That is a different approach, not a
// parameter change. Confirm ESC protocol before Phase 2 driver work builds
// on this.

#include <cstdint>

namespace revali {
namespace hal {

class MotorPwm {
public:
    // Attaches `pin` to LEDC `channel` (0-15) configured for 50 Hz ESC PWM.
    bool begin(uint8_t pin, uint8_t channel);

    // `pulse_us` is clamped to [1000, 2000]. This HAL has no concept of
    // arming/disarming or safety limits — that policy belongs to the motor
    // manager and Safety (Phase 2 / Phase 9), not here.
    void write_pulse_us(uint16_t pulse_us);

private:
    uint8_t channel_ = 0;
};

}  // namespace hal
}  // namespace revali
