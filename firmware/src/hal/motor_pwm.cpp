#include "hal/motor_pwm.h"

#include <Arduino.h>

namespace revali {
namespace hal {

namespace {
constexpr uint32_t kFrequencyHz = 50;
constexpr uint8_t kResolutionBits = 16;
constexpr uint32_t kPeriodUs = 1000000 / kFrequencyHz;  // 20000 us
}  // namespace

bool MotorPwm::begin(uint8_t pin, uint8_t channel) {
    channel_ = channel;
    ledcSetup(channel_, kFrequencyHz, kResolutionBits);
    ledcAttachPin(pin, channel_);
    return true;
}

void MotorPwm::write_pulse_us(uint16_t pulse_us) {
    if (pulse_us < 1000) {
        pulse_us = 1000;
    } else if (pulse_us > 2000) {
        pulse_us = 2000;
    }

    const uint32_t max_duty = (1u << kResolutionBits) - 1;
    const uint32_t duty = static_cast<uint32_t>(
        (static_cast<uint64_t>(pulse_us) * max_duty) / kPeriodUs);
    ledcWrite(channel_, duty);
}

}  // namespace hal
}  // namespace revali
