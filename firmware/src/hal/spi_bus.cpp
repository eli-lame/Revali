#include "hal/spi_bus.h"

#include <SPI.h>

namespace revali {
namespace hal {

bool SpiBus::begin(uint32_t clock_hz, uint8_t sclk, uint8_t miso,
                   uint8_t mosi) {
    clock_hz_ = clock_hz;
    // -1 for SS: this bus does not manage chip-select, the caller does.
    SPI.begin(sclk, miso, mosi, -1);
    return true;
}

void SpiBus::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    // ICM-20948 uses SPI mode 0, MSB first.
    SPI.beginTransaction(SPISettings(clock_hz_, SPI_MSBFIRST, SPI_MODE0));
    for (size_t i = 0; i < len; ++i) {
        rx[i] = SPI.transfer(tx ? tx[i] : 0x00);
    }
    SPI.endTransaction();
}

}  // namespace hal
}  // namespace revali
