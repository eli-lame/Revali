#include "hal/i2c_bus.h"

#include <Wire.h>

namespace revali {
namespace hal {

bool I2cBus::begin(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
    return Wire.begin(static_cast<int>(sda_pin), static_cast<int>(scl_pin),
                       clock_hz);
}

bool I2cBus::write(uint8_t address, const uint8_t* data, size_t len) {
    Wire.beginTransmission(address);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

bool I2cBus::read(uint8_t address, uint8_t* data, size_t len) {
    // Wire.requestFrom's uint8_t-size overload — VL53L0X reads are always a
    // handful of bytes, well within range.
    uint8_t got = Wire.requestFrom(address, static_cast<uint8_t>(len));
    if (got != len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        data[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

}  // namespace hal
}  // namespace revali
