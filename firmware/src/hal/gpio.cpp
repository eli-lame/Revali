#include "hal/gpio.h"

#include <Arduino.h>

namespace revali {
namespace hal {

void gpio_configure(uint8_t pin, PinMode mode) {
    switch (mode) {
        case PinMode::Input:
            pinMode(pin, INPUT);
            break;
        case PinMode::InputPullup:
            pinMode(pin, INPUT_PULLUP);
            break;
        case PinMode::Output:
            pinMode(pin, OUTPUT);
            break;
    }
}

void gpio_write(uint8_t pin, bool level) {
    digitalWrite(pin, level ? HIGH : LOW);
}

bool gpio_read(uint8_t pin) {
    return digitalRead(pin) != 0;
}

}  // namespace hal
}  // namespace revali
