#pragma once
//
// hal/gpio.h — thin wrapper over digital GPIO.
//
// Purpose: hide Arduino's pinMode/digitalWrite/digitalRead behind this seam
// so driver code depends on the HAL, not directly on Arduino.h. See
// docs/architecture.md "HAL" module — this is deliberately a plain function
// wrapper, not an interface class; there is only one implementation and none
// is planned, so a virtual seam here would violate the project's "no
// interface without a second implementation" rule.

#include <cstdint>

namespace revali {
namespace hal {

enum class PinMode : uint8_t {
    Input,
    InputPullup,
    Output,
};

void gpio_configure(uint8_t pin, PinMode mode);
void gpio_write(uint8_t pin, bool level);
bool gpio_read(uint8_t pin);

}  // namespace hal
}  // namespace revali
