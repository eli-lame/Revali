#pragma once
//
// hal/adc.h — thin wrapper over the ESP32 ADC, ADC1 only.
//
// ADC2 must never be used — it is unusable once the radio is active (WiFi,
// and therefore ESP-NOW), which on this project is effectively always. See
// docs/hardware.md. This wrapper does not enforce that at compile time (pin
// validity isn't knowable from a bare GPIO number without a lookup table),
// so the caller is responsible for only ever passing an ADC1-capable pin —
// on the vehicle, that is board::kBatterySense (GPIO 34) and nothing else.
//
// Uses analogReadMilliVolts(), not analogRead(), because it applies the
// ESP32's built-in eFuse ADC calibration — this is the concrete fix for the
// "ADC is nonlinear near both rails" warning in docs/hardware.md, not a
// separate calibration step layered on top.

#include <cstdint>

namespace revali {
namespace hal {

// Returns the pin's voltage in millivolts, calibrated.
uint32_t adc_read_mv(uint8_t pin);

}  // namespace hal
}  // namespace revali
