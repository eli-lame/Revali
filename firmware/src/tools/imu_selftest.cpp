// IMU SPI self-test — docs/current_devtasks.md task [2.2] precursor.
//
// Purpose: prove the ICM-20948 is wired correctly and talking over SPI,
// BEFORE any real driver work depends on it. This is a bring-up tool, not
// flight code:
//
//   - It polls. The flight driver must use the data-ready interrupt on
//     GPIO 35 instead (docs/hardware.md, docs/estimator.md).
//   - It calls Serial.print from the sample path, which the 1 kHz loop is
//     never allowed to do (CLAUDE.md conventions).
//   - It leaves the sensor in its power-on default ranges (+/-2 g,
//     +/-250 dps) rather than the flight configuration.
//
// What a PASS looks like, with the board sitting still on the bench:
//   - WHO_AM_I reads 0xEA
//   - |accel| is about 9.81 m/s^2 (one g, whatever the orientation)
//   - gyro is near zero on all three axes, drifting a few tenths of a deg/s
//   - temperature is plausible room temperature
//   - tilting the board moves gravity between the accel axes
//
// Build and run:
//   cd firmware && pio run -e imu_test -t upload && pio device monitor

#include <Arduino.h>

#include "board/pins.h"
#include "hal/gpio.h"
#include "hal/spi_bus.h"

using namespace revali;

namespace {

// --- ICM-20948 register map (user bank 0 unless noted) --------------------
constexpr uint8_t kRegBankSel = 0x7F;  // present in every bank
constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegUserCtrl = 0x03;
constexpr uint8_t kRegPwrMgmt1 = 0x06;
constexpr uint8_t kRegPwrMgmt2 = 0x07;
constexpr uint8_t kRegAccelXoutH = 0x2D;  // 0x2D..0x3A = accel, gyro, temp

constexpr uint8_t kWhoAmIExpected = 0xEA;

constexpr uint8_t kPwrMgmt1DeviceReset = 0x80;
constexpr uint8_t kPwrMgmt1ClkAuto = 0x01;  // also clears SLEEP
constexpr uint8_t kPwrMgmt2AllSensorsOn = 0x00;
constexpr uint8_t kUserCtrlI2cIfDis = 0x10;  // SPI-only; ignore the I2C pins

// Sensitivity at the power-on default full-scale ranges. If you change the
// ranges in bank 2, these must change with them.
constexpr float kAccelLsbPerG = 16384.0f;    // +/-2 g
constexpr float kGyroLsbPerDps = 131.0f;     // +/-250 dps
constexpr float kGravityMs2 = 9.80665f;
constexpr float kTempSensitivity = 333.87f;  // LSB/degC
constexpr float kTempOffsetC = 21.0f;

// The flight clock from docs/hardware.md. Bring-up passed at 1 MHz first,
// which proved the wiring; running at 7 MHz here proves the wiring is good at
// the speed the real driver will actually use. Signal integrity problems that
// 1 MHz hides -- long jumpers, a marginal solder joint, crosstalk -- show up
// as garbled WHO_AM_I or intermittent samples at 7 MHz.
//
// If this fails and 1 MHz passed, the wiring is marginal rather than wrong.
// Fix it here; do not work around it by slowing the flight driver down.
constexpr uint32_t kSelfTestClockHz = 7000000;

hal::SpiBus spi;
bool imu_ready = false;

void cs_select() { hal::gpio_write(board::kImuCs, false); }
void cs_release() { hal::gpio_write(board::kImuCs, true); }

uint8_t read_reg(uint8_t reg) {
    uint8_t tx[2] = {static_cast<uint8_t>(reg | 0x80), 0x00};  // MSB = read
    uint8_t rx[2] = {0, 0};
    cs_select();
    spi.transfer(tx, rx, 2);
    cs_release();
    return rx[1];
}

void read_regs(uint8_t reg, uint8_t* out, size_t len) {
    // One transaction: address byte, then len dummy bytes clocked out while
    // the device streams consecutive registers back.
    if (len > 32) {
        return;
    }
    uint8_t tx[1 + 32] = {static_cast<uint8_t>(reg | 0x80)};
    uint8_t rx[1 + 32] = {0};
    cs_select();
    spi.transfer(tx, rx, len + 1);
    cs_release();
    for (size_t i = 0; i < len; ++i) {
        out[i] = rx[i + 1];
    }
}

void write_reg(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {static_cast<uint8_t>(reg & 0x7F), value};  // MSB clear
    uint8_t rx[2] = {0, 0};
    cs_select();
    spi.transfer(tx, rx, 2);
    cs_release();
}

void select_bank(uint8_t bank) {
    write_reg(kRegBankSel, static_cast<uint8_t>(bank << 4));
}

void explain_who_am_i(uint8_t value) {
    Serial.println();
    Serial.println("  WHO_AM_I did not match. Most likely causes:");
    if (value == 0x00) {
        Serial.println("  0x00 = MISO is being held low. Usually the sensor");
        Serial.println("  has no power, MISO is not connected, or CS never");
        Serial.println("  actually reaches the chip. Measure 3V3 at the IMU");
        Serial.println("  first -- it must be 3.3 V, NOT 5 V.");
    } else if (value == 0xFF) {
        Serial.println("  0xFF = MISO is floating. The MISO wire is open or");
        Serial.println("  on the wrong GPIO. Nothing is driving the line.");
    } else {
        Serial.println("  A garbled value usually means MOSI and MISO are");
        Serial.println("  swapped, the clock is too fast for the wiring, or");
        Serial.println("  the SPI mode is wrong (try SPI_MODE3 in");
        Serial.println("  src/hal/spi_bus.cpp -- the ICM-20948 accepts both).");
    }
    Serial.println();
    Serial.println("  Check continuity against docs/hardware.md:");
    Serial.printf("    SCLK GPIO %u, MISO GPIO %u, MOSI GPIO %u, CS GPIO %u\n",
                  board::kImuSclk, board::kImuMiso, board::kImuMosi,
                  board::kImuCs);
}

bool imu_begin() {
    hal::gpio_configure(board::kImuCs, hal::PinMode::Output);
    cs_release();  // CS idles high

    spi.begin(kSelfTestClockHz, board::kImuSclk, board::kImuMiso,
              board::kImuMosi);
    delay(10);

    select_bank(0);
    uint8_t who = read_reg(kRegWhoAmI);
    Serial.printf("WHO_AM_I = 0x%02X (expect 0x%02X)\n", who, kWhoAmIExpected);
    if (who != kWhoAmIExpected) {
        explain_who_am_i(who);
        return false;
    }
    Serial.println("  -> SPI link to the ICM-20948 is good.");

    // Full reset, then wake. After DEVICE_RESET the part comes back asleep,
    // so SLEEP has to be cleared explicitly and the bank reselected.
    write_reg(kRegPwrMgmt1, kPwrMgmt1DeviceReset);
    delay(100);
    select_bank(0);
    write_reg(kRegPwrMgmt1, kPwrMgmt1ClkAuto);  // clears SLEEP
    delay(20);
    write_reg(kRegPwrMgmt2, kPwrMgmt2AllSensorsOn);
    delay(20);
    write_reg(kRegUserCtrl, kUserCtrlI2cIfDis);
    delay(10);

    uint8_t pwr = read_reg(kRegPwrMgmt1);
    Serial.printf("PWR_MGMT_1 = 0x%02X (SLEEP bit must be clear)\n", pwr);
    if (pwr & 0x40) {
        Serial.println("  -> still asleep; writes are not landing.");
        return false;
    }

    Serial.println("  -> accel and gyro powered, default ranges");
    Serial.println("     (+/-2 g, +/-250 dps).");
    return true;
}

int16_t be16(uint8_t hi, uint8_t lo) {
    return static_cast<int16_t>((static_cast<uint16_t>(hi) << 8) | lo);
}

void print_sample() {
    uint8_t raw[14] = {0};
    read_regs(kRegAccelXoutH, raw, sizeof(raw));

    int16_t ax = be16(raw[0], raw[1]);
    int16_t ay = be16(raw[2], raw[3]);
    int16_t az = be16(raw[4], raw[5]);
    int16_t gx = be16(raw[6], raw[7]);
    int16_t gy = be16(raw[8], raw[9]);
    int16_t gz = be16(raw[10], raw[11]);
    int16_t t = be16(raw[12], raw[13]);

    // SI units at the driver boundary, per CLAUDE.md: nothing downstream of
    // a driver ever sees raw LSBs.
    float ax_ms2 = ax / kAccelLsbPerG * kGravityMs2;
    float ay_ms2 = ay / kAccelLsbPerG * kGravityMs2;
    float az_ms2 = az / kAccelLsbPerG * kGravityMs2;

    float gx_dps = gx / kGyroLsbPerDps;
    float gy_dps = gy / kGyroLsbPerDps;
    float gz_dps = gz / kGyroLsbPerDps;

    float accel_mag =
        sqrtf(ax_ms2 * ax_ms2 + ay_ms2 * ay_ms2 + az_ms2 * az_ms2);
    float temp_c = (t / kTempSensitivity) + kTempOffsetC;

    Serial.printf(
        "a[%7.2f %7.2f %7.2f] m/s^2  |a|=%5.2f   "
        "g[%8.2f %8.2f %8.2f] deg/s   %4.1f C\n",
        ax_ms2, ay_ms2, az_ms2, accel_mag, gx_dps, gy_dps, gz_dps, temp_c);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);  // let USB-serial settle before the first prints

    Serial.println();
    Serial.println("Revali -- ICM-20948 SPI self-test");
    Serial.println("=================================");
    Serial.printf("SCLK %u  MISO %u  MOSI %u  CS %u  (INT %u, unused here)\n",
                  board::kImuSclk, board::kImuMiso, board::kImuMosi,
                  board::kImuCs, board::kImuInt);
    Serial.printf("SPI clock %lu Hz\n",
                  static_cast<unsigned long>(kSelfTestClockHz));
    Serial.println();

    imu_ready = imu_begin();

    Serial.println();
    if (imu_ready) {
        Serial.println("Sitting still, |a| should read about 9.81 m/s^2 and");
        Serial.println("all three gyro axes should sit near zero. Tilt the");
        Serial.println("board and gravity should move between accel axes.");
    } else {
        Serial.println("Self-test FAILED. Fix the wiring above before going");
        Serial.println("any further -- nothing downstream is worth debugging");
        Serial.println("until WHO_AM_I reads back 0xEA.");
    }
    Serial.println();
}

void loop() {
    if (!imu_ready) {
        delay(2000);
        Serial.println("IMU not initialised -- see the messages above.");
        return;
    }

    // Bus integrity check. At 7 MHz the failure mode is not a dead link but
    // an INTERMITTENT one, and a human reading float columns will not notice
    // one corrupted sample in five hundred. WHO_AM_I is a known constant, so
    // re-reading it every pass turns that into a number you can watch.
    static uint32_t reads = 0;
    static uint32_t bus_errors = 0;
    ++reads;
    if (read_reg(kRegWhoAmI) != kWhoAmIExpected) {
        ++bus_errors;
    }

    print_sample();

    if (reads % 50 == 0) {
        Serial.printf("   [bus check: %lu/%lu WHO_AM_I reads bad]%s\n",
                      static_cast<unsigned long>(bus_errors),
                      static_cast<unsigned long>(reads),
                      bus_errors == 0 ? "" : "  <-- SIGNAL INTEGRITY PROBLEM");
    }

    delay(100);  // 10 Hz, readable in a terminal
}
