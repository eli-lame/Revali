// Phase 1 wiring-verification tool — docs/current_devtasks.md tasks
// [1.3]/[1.4]: "blink an LED, print at 115200, scan the I2C bus and see
// devices."
//
// What this checks, and what it doesn't:
//   - I2C bus scan: verifies SDA/SCL wiring and that the ToF pair answers.
//   - XSHUT toggle test: proves each XSHUT line is wired to the CORRECT
//     sensor by watching devices appear/disappear from the bus scan as each
//     is released from reset — directly exercises the exact failure mode
//     docs/hardware.md warns about (both sensors default to 0x29).
//   - Battery ADC: prints millivolts so you can cross-check against a
//     multimeter and confirm the divider is wired correctly.
//   - IMU SPI: a best-effort WHO_AM_I read. This is informal and will be
//     superseded by the real IImuSource driver in Phase 2 task [2.2] — it
//     exists here only to prove the SPI wiring is electrically sound before
//     real driver work depends on it.
//   - ESC idle hold: PROPS MUST BE OFF. Holds all four ESC signal lines at
//     a steady 1000 us pulse — the standard "disarmed/idle" position for
//     analog PWM ESCs — using hal::MotorPwm. This is NOT the Phase 2 motor
//     manager: there is no arming, no ramping, no per-motor test command,
//     just a constant idle signal so (a) the "no signal" beeping stops and
//     (b) each ESC signal wire's continuity is confirmed by the ESC's own
//     startup/arm tone. Real motor control is still Phase 2 [2.12]/[2.13].

#include <Arduino.h>
#include <Wire.h>

#include "board/pins.h"
#include "hal/adc.h"
#include "hal/gpio.h"
#include "hal/motor_pwm.h"
#include "hal/spi_bus.h"
#include "hal/timing.h"

using namespace revali;

namespace {

hal::SpiBus imu_spi;

hal::MotorPwm esc_front_left;
hal::MotorPwm esc_front_right;
hal::MotorPwm esc_rear_right;
hal::MotorPwm esc_rear_left;

constexpr uint16_t kIdlePulseUs = 1000;
constexpr uint16_t kSpinTestPulseUs = 1500;  // well clear of any idle deadband
constexpr uint32_t kSpinTestMs = 1500;

struct MotorEntry {
    hal::MotorPwm* esc;
    const char* name;
};

void i2c_scan(const char* label) {
    Serial.printf("I2C scan (%s):\n", label);
    int found = 0;
    for (uint8_t addr = 0x01; addr < 0x7F; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  found device at 0x%02X\n", addr);
            ++found;
        }
    }
    if (found == 0) {
        Serial.println("  (nothing responded)");
    }
}

void xshut_continuity_check() {
    Serial.println();
    Serial.println("--- ToF XSHUT continuity check ---");

    hal::gpio_configure(board::kTofAXshut, hal::PinMode::Output);
    hal::gpio_configure(board::kTofBXshut, hal::PinMode::Output);

    // Both held in reset: bus should be silent.
    hal::gpio_write(board::kTofAXshut, false);
    hal::gpio_write(board::kTofBXshut, false);
    delay(5);
    i2c_scan("both XSHUT low, expect nothing");

    // Release A only: expect exactly one device, at the power-on default
    // address 0x29. "Release" is high-impedance, NOT driven high — the
    // breakout pulls XSHUT up through 47 kOhm to its internal 2.8 V rail,
    // so driving 3.3 V at it back-feeds current for nothing. See
    // docs/hardware.md "Release means high-impedance, not driven high".
    hal::gpio_configure(board::kTofAXshut, hal::PinMode::Input);
    delay(5);
    i2c_scan("A released, B held low, expect one device at 0x29");

    // Release B too: both now answer at 0x29 (address conflict, expected —
    // this is exactly why Phase 2 must run the reassignment sequence before
    // either sensor is usable). Seeing 0x29 respond here is a PASS for
    // wiring purposes; the conflict itself is normal and handled later.
    hal::gpio_configure(board::kTofBXshut, hal::PinMode::Input);
    delay(5);
    i2c_scan("both released, expect 0x29 (address conflict is normal/expected)");

    Serial.println("If a scan step didn't match its expectation, recheck");
    Serial.printf("that XSHUT A/B (GPIO %u/%u) go to the correct sensor,\n",
                  board::kTofAXshut, board::kTofBXshut);
    Serial.printf("and that SDA/SCL (GPIO %u/%u) are wired to both.\n",
                  board::kI2cSda, board::kI2cScl);
    Serial.println("-----------------------------------");
}

void imu_who_am_i_check() {
    Serial.println();
    Serial.println("--- IMU SPI continuity check (informal) ---");

    hal::gpio_configure(board::kImuCs, hal::PinMode::Output);
    hal::gpio_write(board::kImuCs, true);  // idle high

    imu_spi.begin(1000000, board::kImuSclk, board::kImuMiso,
                  board::kImuMosi);  // slow clock for this bring-up check

    uint8_t tx[2] = {0x00 | 0x80, 0x00};  // WHO_AM_I register, read bit set
    uint8_t rx[2] = {0, 0};

    hal::gpio_write(board::kImuCs, false);
    imu_spi.transfer(tx, rx, 2);
    hal::gpio_write(board::kImuCs, true);

    Serial.printf("WHO_AM_I read = 0x%02X (expect 0xEA on a wired ICM-20948)\n",
                  rx[1]);
    if (rx[1] != 0xEA) {
        Serial.printf(
            "Mismatch — check SCLK/MISO/MOSI/CS (GPIO %u/%u/%u/%u)\n",
            board::kImuSclk, board::kImuMiso, board::kImuMosi, board::kImuCs);
        Serial.println("and that the IMU is on 3V3, not 5 V.");
    }
    Serial.println("--------------------------------------------");
}

void esc_idle_hold() {
    Serial.println();
    Serial.println("--- ESC idle hold (PROPS MUST BE OFF) ---");
    Serial.println("Holding all four ESC signal lines at 1000 us (idle).");
    Serial.println("This should silence the ESC 'no signal' beep — if a");
    Serial.println("motor spins, or beeping continues, STOP and recheck");
    Serial.println("that signal wire's GPIO before going further.");

    esc_front_left.begin(board::kMotorFrontLeft, 0);
    esc_front_right.begin(board::kMotorFrontRight, 1);
    esc_rear_right.begin(board::kMotorRearRight, 2);
    esc_rear_left.begin(board::kMotorRearLeft, 3);

    // LEDC is hardware-generated and free-running once configured — writing
    // the pulse once here is sufficient. No per-tick refresh needed in
    // loop(), unlike a bit-banged signal.
    esc_front_left.write_pulse_us(kIdlePulseUs);
    esc_front_right.write_pulse_us(kIdlePulseUs);
    esc_rear_right.write_pulse_us(kIdlePulseUs);
    esc_rear_left.write_pulse_us(kIdlePulseUs);

    Serial.println("All four channels holding 1000 us.");
    Serial.println("------------------------------------------");
}

MotorEntry motor_for_command(char c) {
    switch (c) {
        case '1':
            return {&esc_front_left, "M1 front-left"};
        case '2':
            return {&esc_front_right, "M2 front-right"};
        case '3':
            return {&esc_rear_right, "M3 rear-right"};
        case '4':
            return {&esc_rear_left, "M4 rear-left"};
        default:
            return {nullptr, ""};
    }
}

void handle_spin_test_input() {
    if (!Serial.available()) {
        return;
    }
    char c = static_cast<char>(Serial.read());
    MotorEntry entry = motor_for_command(c);
    if (entry.esc == nullptr) {
        return;
    }

    Serial.printf("Spinning %s at %u us for %lu ms (props must be off)...\n",
                  entry.name, kSpinTestPulseUs,
                  static_cast<unsigned long>(kSpinTestMs));
    entry.esc->write_pulse_us(kSpinTestPulseUs);
    delay(kSpinTestMs);
    entry.esc->write_pulse_us(kIdlePulseUs);
    Serial.printf("%s back to idle.\n", entry.name);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);  // let USB-serial settle before the first prints

    hal::gpio_configure(board::kStatusLed, hal::PinMode::Output);

    Serial.println();
    Serial.println("Revali hopcopter FC — Phase 1 wiring check");
    Serial.println("===========================================");

    Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cClockHz);
    xshut_continuity_check();
    imu_who_am_i_check();
    esc_idle_hold();

    Serial.println();
    Serial.println("Type 1/2/3/4 in the serial monitor to briefly spin that");
    Serial.println("motor (M1 front-left, M2 front-right, M3 rear-right,");
    Serial.println("M4 rear-left) and confirm it's wired to the position");
    Serial.println("you expect. PROPS MUST BE OFF. All other motors stay");
    Serial.println("at idle while one spins.");
    Serial.println();
    Serial.println("Heartbeat (LED blink + battery mV) continues every 1s.");
}

void loop() {
    handle_spin_test_input();

    static uint32_t last_heartbeat_ms = 0;
    uint32_t now = millis();
    if (now - last_heartbeat_ms < 1000) {
        return;
    }
    last_heartbeat_ms = now;

    static bool led_on = false;
    led_on = !led_on;
    hal::gpio_write(board::kStatusLed, led_on);

    uint32_t battery_mv = hal::adc_read_mv(board::kBatterySense);
    Serial.printf("[%lu ms] battery sense = %lu mV\n", now,
                  static_cast<unsigned long>(battery_mv));
}
