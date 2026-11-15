// ToF I2C self-test — docs/current_devtasks.md task [2.5] precursor.
//
// Brings up the VL53L0X pair over I2C and streams range, so the wiring and
// the XSHUT address dance can be proven before the estimator depends on them.
// Works with ONE sensor wired (ToF A alone) or both.
//
// This is a bring-up tool, not flight code: it polls, it prints from the
// sample path, and it does no tilt compensation or ground-plane fit. See
// docs/estimator.md "Height: two downward sensors" for what the real thing
// has to do with these numbers.
//
// THE TWO WAYS THIS HARDWARE BITES, both from docs/hardware.md:
//
//   1. VCC must be 3.3 V, NEVER 5 V. The breakout's I2C pull-ups go to VIN
//      itself, so 5 V on VIN puts 5 V on GPIO 21/22, which are not 5 V
//      tolerant. Check this with a meter before plugging anything in.
//
//   2. "Release XSHUT" means set the GPIO to INPUT, not drive it high. The
//      pin is pulled up by 47 kOhm to the breakout's internal 2.8 V rail.
//      Driving 3.3 V into it works but back-feeds current for no reason.
//
// Build and run:
//   pio run -e tof_test -t upload ; pio device monitor

#include <Arduino.h>
#include <VL53L0X.h>
#include <Wire.h>

#include "board/pins.h"
#include "hal/gpio.h"

using namespace revali;

namespace {

// Address A is moved to once B is about to be released. B keeps the
// power-on default. See docs/hardware.md "Wire each sensor's XSHUT pin to
// its own GPIO and sequence them at boot".
constexpr uint8_t kAddrDefault = 0x29;
constexpr uint8_t kAddrTofA = 0x30;

// Two range profiles, switchable at runtime so they can be compared against
// the SAME target rather than argued about.
//
// FAST is ~50 Hz, the rate docs/estimator.md assumes. SHORT BUDGETS COST
// MAXIMUM RANGE -- that is the whole tradeoff, and it is why this is
// measurable rather than hardcoded.
//
// LONG is the standard VL53L0X long-range configuration: drop the signal
// rate limit so dimmer returns are accepted, and lengthen both VCSEL pulse
// periods so more light goes out per measurement. Costs sample rate and
// raises susceptibility to ambient IR.
constexpr uint32_t kBudgetFastUs = 20000;  // ~50 Hz
constexpr uint32_t kBudgetLongUs = 50000;  // ~20 Hz

constexpr float kSignalRateDefault = 0.25f;  // MCPS, library default
constexpr float kSignalRateLong = 0.10f;

bool long_range_mode = false;

VL53L0X tof_a;
VL53L0X tof_b;

bool have_a = false;
bool have_b = false;

void hold_both_in_reset() {
    hal::gpio_configure(board::kTofAXshut, hal::PinMode::Output);
    hal::gpio_configure(board::kTofBXshut, hal::PinMode::Output);
    hal::gpio_write(board::kTofAXshut, false);
    hal::gpio_write(board::kTofBXshut, false);
    delay(10);
}

// "Release" = high-impedance. The breakout's own pull-up raises the pin to
// its 2.8 V rail. This is deliberately NOT gpio_write(pin, true).
void release(uint8_t pin) {
    hal::gpio_configure(pin, hal::PinMode::Input);
    delay(5);  // datasheet boot time is ~1.2 ms; 5 ms is free here
}

int bus_scan(const char* label) {
    Serial.printf("I2C scan (%s):\n", label);
    int found = 0;
    for (uint8_t addr = 0x01; addr < 0x7F; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("   found 0x%02X\n", addr);
            ++found;
        }
    }
    if (found == 0) {
        Serial.println("   (nothing responded)");
    }
    return found;
}

// Applies whichever profile long_range_mode currently selects. Safe to call
// on a sensor that is already ranging.
void apply_profile(VL53L0X& sensor, bool present) {
    if (!present) {
        return;
    }
    sensor.stopContinuous();
    delay(5);
    if (long_range_mode) {
        sensor.setSignalRateLimit(kSignalRateLong);
        sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
        sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);
        sensor.setMeasurementTimingBudget(kBudgetLongUs);
    } else {
        sensor.setSignalRateLimit(kSignalRateDefault);
        sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 14);
        sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 10);
        sensor.setMeasurementTimingBudget(kBudgetFastUs);
    }
    sensor.startContinuous();
}

void announce_profile() {
    if (long_range_mode) {
        Serial.printf("\n>> LONG RANGE: %lu us budget (~20 Hz), signal rate "
                      "limit %.2f MCPS\n\n",
                      static_cast<unsigned long>(kBudgetLongUs),
                      kSignalRateLong);
    } else {
        Serial.printf("\n>> FAST: %lu us budget (~50 Hz), signal rate limit "
                      "%.2f MCPS\n\n",
                      static_cast<unsigned long>(kBudgetFastUs),
                      kSignalRateDefault);
    }
}

bool configure(VL53L0X& sensor, uint8_t addr, const char* name) {
    sensor.setBus(&Wire);
    sensor.setAddress(addr);
    if (!sensor.init()) {
        Serial.printf("   %s: init() FAILED at 0x%02X\n", name, addr);
        return false;
    }
    sensor.setTimeout(500);
    apply_profile(sensor, true);
    Serial.printf("   %s: ranging at 0x%02X\n", name, addr);
    return true;
}

void bring_up() {
    Serial.println("--- XSHUT sequence ---");
    hold_both_in_reset();
    if (bus_scan("both held in reset, expect nothing") != 0) {
        Serial.println("   !! Something answered while both XSHUT are low.");
        Serial.println("   !! XSHUT is probably not reaching the sensor --");
        Serial.println("   !! check GPIO 18 / 26 continuity.");
    }

    // --- ToF A -----------------------------------------------------------
    release(board::kTofAXshut);
    Serial.println();
    int n = bus_scan("A released, expect exactly one device at 0x29");
    if (n == 0) {
        Serial.println("   ToF A did not appear. Check VCC is 3V3 (NOT 5 V),");
        Serial.println("   GND, and SDA/SCL on GPIO 22/21.");
    } else {
        have_a = configure(tof_a, kAddrTofA, "ToF A (front-left)");
        if (have_a) {
            Serial.printf("   moved ToF A to 0x%02X so B can take 0x%02X\n",
                          kAddrTofA, kAddrDefault);
        }
    }

    // --- ToF B, if it is wired at all ------------------------------------
    release(board::kTofBXshut);
    Serial.println();
    n = bus_scan("B released");
    bool b_present = false;
    for (uint8_t addr = 0x01; addr < 0x7F && !b_present; ++addr) {
        if (addr == kAddrTofA) {
            continue;
        }
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0 && addr == kAddrDefault) {
            b_present = true;
        }
    }
    if (b_present) {
        have_b = configure(tof_b, kAddrDefault, "ToF B (rear-right)");
    } else {
        Serial.println("   No second sensor at 0x29 -- running with ToF A");
        Serial.println("   only. That is expected if B is not wired yet.");
    }

    // --- The verification docs/hardware.md says never to skip -------------
    Serial.println();
    Serial.println("--- Verify ---");
    if (have_a && have_b) {
        Serial.println("   Two sensors, distinct addresses. Correct.");
        Serial.println("   Block ONE sensor with your hand and confirm only");
        Serial.println("   that column changes -- if both move together you");
        Serial.println("   are reading one sensor twice, which looks");
        Serial.println("   perfectly plausible and is completely wrong.");
    } else if (have_a) {
        Serial.println("   One sensor (A). Add B before trusting any");
        Serial.println("   ground-slope or touchdown logic.");
    } else {
        Serial.println("   No sensor is ranging. Fix the wiring above.");
    }
    Serial.println("----------------------");
    Serial.println();
}

// Every sample line carries its profile. A bring-up log gets pasted into a
// chat or a doc hours later, stripped of whatever banner scrolled past, and
// a reading means nothing without knowing which configuration produced it.
void print_profile_tag() { Serial.print(long_range_mode ? "[LONG] " : "[FAST] "); }

void print_one(VL53L0X& sensor, bool present, const char* name) {
    if (!present) {
        return;
    }
    uint16_t mm = sensor.readRangeContinuousMillimeters();
    if (sensor.timeoutOccurred()) {
        Serial.printf("  %s: TIMEOUT   ", name);
    } else if (mm >= 8190) {
        // 8190/8191 is the library's out-of-range sentinel, not a distance.
        Serial.printf("  %s: out of range   ", name);
    } else {
        Serial.printf("  %s: %4u mm (%5.2f m)   ", name, mm, mm / 1000.0f);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("Revali -- VL53L0X ToF self-test");
    Serial.println("===============================");
    Serial.printf("SDA %u  SCL %u  @ %lu Hz\n", board::kI2cSda,
                  board::kI2cScl,
                  static_cast<unsigned long>(board::kI2cClockHz));
    Serial.printf("XSHUT A %u   XSHUT B %u\n", board::kTofAXshut,
                  board::kTofBXshut);
    Serial.println("Sensor VCC must be 3V3. If you gave it 5 V, stop now.");
    Serial.println();

    Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cClockHz);
    bring_up();

    if (have_a || have_b) {
        announce_profile();
        Serial.println("Press 'l' to toggle FAST <-> LONG RANGE while it");
        Serial.println("runs, so both profiles can be measured against the");
        Serial.println("SAME target. Find the distance where each one starts");
        Serial.println("reporting out of range -- that number, not the");
        Serial.println("datasheet's, is what the hop controller has to live");
        Serial.println("with.");
        Serial.println();
        Serial.println("Aim at a flat wall square-on, back away slowly, and");
        Serial.println("note where it drops out. Then press 'l' and repeat.");
        Serial.println();
    }
}

void handle_input() {
    if (!Serial.available()) {
        return;
    }
    char c = static_cast<char>(Serial.read());
    if (c != 'l' && c != 'L') {
        return;
    }
    long_range_mode = !long_range_mode;
    apply_profile(tof_a, have_a);
    apply_profile(tof_b, have_b);
    announce_profile();
}

void loop() {
    if (!have_a && !have_b) {
        Serial.println("No sensor -- see the messages above.");
        delay(2000);
        return;
    }
    handle_input();
    print_profile_tag();
    print_one(tof_a, have_a, "A");
    print_one(tof_b, have_b, "B");
    Serial.println();
    delay(100);  // 10 Hz, readable in a terminal
}
