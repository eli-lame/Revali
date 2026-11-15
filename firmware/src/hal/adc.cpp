#include "hal/adc.h"

#include <Arduino.h>

namespace revali {
namespace hal {

uint32_t adc_read_mv(uint8_t pin) {
    return analogReadMilliVolts(pin);
}

}  // namespace hal
}  // namespace revali
