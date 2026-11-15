#pragma once
//
// hal/spi_bus.h — thin wrapper over the ESP32's VSPI peripheral.
//
// Sole consumer today is the ICM-20948 (see docs/hardware.md "Put the IMU on
// SPI, not I2C" — 7 MHz is chosen there specifically to keep a 12-byte burst
// read at roughly 16 us, well under the 1 kHz tick budget). Chip-select is
// managed by the caller (per-device), not by this bus, since a future second
// SPI device would need its own CS line regardless.

#include <cstddef>
#include <cstdint>

namespace revali {
namespace hal {

class SpiBus {
public:
    // Brings up VSPI on explicit pins. `clock_hz` is applied per-transaction
    // in transfer().
    //
    // The pins MUST be passed: none of this board's SPI lines sits on an
    // ESP32 VSPI default (18/19/23/5), so a bare SPI.begin() silently drives
    // the wrong GPIOs and every transfer reads back 0x00 or 0xFF. See
    // docs/hardware.md "Every SPI pin and both I2C pins are off their
    // framework defaults".
    //
    // CS is deliberately not passed: chip-select is per-device and managed by
    // the caller, so the underlying driver is told not to drive an SS line.
    bool begin(uint32_t clock_hz, uint8_t sclk, uint8_t miso, uint8_t mosi);

    // Full-duplex transfer: writes `len` bytes from `tx`, reads `len` bytes
    // into `rx`. `tx` may be nullptr to send zeros (read-only transaction).
    // Caller is responsible for asserting/releasing CS around this call.
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len);

private:
    uint32_t clock_hz_ = 1000000;
};

}  // namespace hal
}  // namespace revali
