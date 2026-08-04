#pragma once
//
// Core data contracts for the Revali hopcopter flight controller.
//
// These structs are the shared vocabulary between modules. They contain no
// logic and no hardware knowledge — only plain data. Every module compiles
// against this header; changing a contract here is a deliberate, cross-cutting
// decision.
//
// See docs/data_model.md for the design intent behind each type.

#include <cstdint>

namespace revali {

// ---------------------------------------------------------------------------
// Small math primitives
// ---------------------------------------------------------------------------

// A 3-axis vector. Body frame unless a field's comment says otherwise.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Unit quaternion (w, x, y, z), body-to-world rotation. Identity by default.
struct Quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// ---------------------------------------------------------------------------
// Fixed capacities
// ---------------------------------------------------------------------------

// The hopcopter is a quadrotor: four motors in an X layout.
inline constexpr uint8_t kMaxMotors = 4;

// ---------------------------------------------------------------------------
// Sensor samples
// ---------------------------------------------------------------------------

// Raw sample from the ICM-20948 (accel + gyro + mag). Owned by an IImuSource
// driver, consumed by the estimator. See docs/estimator.md.
struct ImuData {
    uint32_t timestamp_us = 0;   // micros() when the sample was taken
    Vec3 accel;                  // m/s^2, body frame
    Vec3 gyro;                   // rad/s, body frame
    Vec3 mag;                    // uT, body frame; zero if mag is disabled
    bool mag_valid = false;      // magnetometer runs slower than accel/gyro
    float temperature_c = 0.0f;  // optional; 0 if the chip doesn't report it
    bool valid = false;          // false => stale/failed read, do not trust
};

// One downward range measurement from a VL53L0X. Owned by a range driver,
// consumed by the estimator's height filter and by ground-contact detection.
struct RangeData {
    uint32_t timestamp_us = 0;
    float distance_m = 0.0f;     // along the sensor's own boresight, not vertical
    bool valid = false;          // false => out of range, bad status, or stale
};

// Battery health. Owned by the battery driver, consumed by Safety/Telemetry.
// NOTE: launch bursts sag the pack hard. Failsafe thresholds are evaluated
// against `voltage_v` (filtered); `voltage_raw_v` is for logging only.
struct BatteryState {
    uint32_t timestamp_us = 0;
    float voltage_v = 0.0f;      // low-pass filtered
    float voltage_raw_v = 0.0f;  // unfiltered, logging only
    float current_a = 0.0f;      // 0 if not measured
    bool valid = false;
};

// ---------------------------------------------------------------------------
// Estimation / control pipeline
// ---------------------------------------------------------------------------

// How much to trust VehicleState::height_m this tick. Consumers make different
// decisions with degraded data: altitude hold needs SINGLE or better, a hop can
// coast on INERTIAL briefly, nothing may act on NONE.
enum class HeightConfidence : uint8_t {
    None = 0,      // no usable height
    Inertial,      // coasting on the accel filter, no valid range
    Single,        // one ToF valid, or the two disagree
    Both,          // both ToF valid and in agreement
};

// Best estimate of the vehicle's state. This is the estimator's output and the
// controller's input. See docs/estimator.md.
struct VehicleState {
    uint32_t timestamp_us = 0;   // time the estimate is valid for

    // Attitude. Quaternion is authoritative; Euler angles are a convenience
    // derived from it for logging/telemetry.
    Quat orientation;
    float roll_rad = 0.0f;
    float pitch_rad = 0.0f;
    float yaw_rad = 0.0f;

    Vec3 angular_velocity;       // rad/s, body frame, bias-corrected

    // Vertical channel. Fused from the ToF pair and the accelerometer.
    float height_m = 0.0f;            // above ground, tilt-compensated
    float vertical_velocity_ms = 0.0f;// world frame, +up; needed for apex detect
    float ground_slope_rad = 0.0f;    // from the ToF pair's disagreement
    HeightConfidence height_confidence = HeightConfidence::None;
    bool ground_contact = false;      // latched + debounced; drives the hop machine

    bool valid = false;          // false => estimate is stale/untrustworthy
};

// What the flight-mode manager wants the vehicle to do. Controller input.
// Yaw is a RATE (the magnetometer is unreliable near loaded motors) and
// altitude is commanded as a climb rate that integrates into `height_m` — the
// joystick never has direct authority over collective thrust.
struct DesiredState {
    uint32_t timestamp_us = 0;
    float roll_rad = 0.0f;       // tilt setpoint
    float pitch_rad = 0.0f;      // tilt setpoint
    float yaw_rate_rads = 0.0f;  // rad/s
    float climb_rate_ms = 0.0f;  // m/s, ALTITUDE axis mode
    float height_m = 0.0f;       // hold target, integrated from climb_rate_ms
    float thrust = 0.0f;         // normalized collective; overridden while HOPPING
    bool use_height_hold = false;// false in attitude-only modes
};

// Controller output: desired torques/force before mixing to motors.
struct ControlOutput {
    uint32_t timestamp_us = 0;
    Vec3 torque;                 // roll/pitch/yaw effort, normalized -1..1
    float thrust = 0.0f;         // normalized collective, 0..1
};

// Final per-motor commands after mixing. Only [0, count) are meaningful.
// `saturated` travels with the output because the controller needs it on the
// NEXT tick to freeze its integrators; recomputing it elsewhere would duplicate
// the mixer's allocation logic.
struct MotorOutput {
    uint32_t timestamp_us = 0;
    uint8_t count = 0;
    float value[kMaxMotors] = {};  // normalized 0..1 per motor
    bool saturated = false;        // mixer hit a limit this tick
    bool armed = false;
};

// ---------------------------------------------------------------------------
// Hop cycle
// ---------------------------------------------------------------------------

// Phases of one hop. Advanced at the full 1 kHz loop rate — touchdown must be
// caught within a few ms or the launch burst misses the extension window.
// See docs/hop_mode.md.
enum class HopPhase : uint8_t {
    Stance = 0,    // foot down, spring compressing
    Launch,        // spring extending, thrust burst applied
    Ascent,        // ballistic, rotors near idle
    Apex,          // vertical velocity crossed zero
    Descent,       // steering the landing point by tilt
};

// Owned by the hop sequencer; consumed by the controller, telemetry, logging.
struct HopState {
    HopPhase phase = HopPhase::Stance;
    uint32_t phase_entered_us = 0;    // for phase timeouts and burst duration
    float last_apex_height_m = 0.0f;  // feeds once-per-hop height regulation
    uint32_t hop_count = 0;
    uint8_t abort_reason = 0;         // 0 => no abort; see docs/hop_mode.md
};

// ---------------------------------------------------------------------------
// Link health
// ---------------------------------------------------------------------------

// Owned by the link, consumed by Safety and telemetry. `last_rx_us` is the
// input to the link-loss failsafe ladder in docs/safety.md.
struct LinkStatus {
    uint32_t last_rx_us = 0;
    uint32_t rx_count = 0;
    uint32_t crc_errors = 0;
    uint32_t seq_gaps = 0;
    bool connected = false;
};

}  // namespace revali
