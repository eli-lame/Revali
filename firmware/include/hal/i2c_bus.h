#pragma once
//
// hal/i2c_bus.h — thin wrapper over the ESP32's I2C (Wire) peripheral.
//
// Sole consumer today is the VL53L0X pair. This bus does NOT implement the
// XSHUT boot address-assignment sequence in docs/hardware.md — that is
// driver-layer logic (it involves GPIO, timing, and knowledge of which
// sensor is which), not a bus concern.

#include <cstddef>
#include <cstdint>

namespace revali {
namespace hal {

class I2cBus {
public:
    bool begin(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz);

    // Returns true if the device ACKed.
    bool write(uint8_t address, const uint8_t* data, size_t len);
    // Returns true only if exactly `len` bytes were read.
    bool read(uint8_t address, uint8_t* data, size_t len);
};

}  // namespace hal
}  // namespace revali
