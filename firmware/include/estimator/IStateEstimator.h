#pragma once
//
// IStateEstimator — the seam between sensing and control.
//
// The hopcopter's implementation is OnboardEstimator: it reads an IImuSource
// (raw accel/gyro from the ICM-20948), runs a Mahony filter for attitude, and
// fuses the downward VL53L0X range into a tilt-compensated height.
//
// The Controller, Safety, and Telemetry modules depend ONLY on this interface
// and on VehicleState — they never touch a sensor driver. That keeps the
// control loop testable against a replayed or synthetic state stream.
//
// Timing contract: update() is PULL-BASED and MUST NOT block. The fixed-rate
// loop calls it every tick. The IMU delivers a fresh sample on nearly every
// tick; the range sensors are much slower, so update() reuses the last height
// between range samples. If data goes stale past the freshness timeout,
// healthy() returns false, which trips Safety's sensor-timeout failsafe.

#include "core/types.h"

namespace charon {

class IStateEstimator {
public:
    virtual ~IStateEstimator() = default;

    // One-time setup (open the sensor/link, zero the filter). Returns false on
    // failure to initialize the underlying source.
    virtual bool begin() = 0;

    // Pull the newest available sensor data and recompute the estimate.
    // `now_us` is the current micros() from the scheduler. Returns true if the
    // state was advanced with fresh IMU data this tick, false if it reused the
    // previous state (no new sample yet). Never blocks.
    virtual bool update(uint32_t now_us) = 0;

    // The current best estimate. Always readable; check .valid / healthy()
    // before acting on it.
    virtual const VehicleState& state() const = 0;

    // False when the estimate has gone stale past the freshness timeout or the
    // source failed. Consumed by Safety's sensor-timeout failsafe.
    virtual bool healthy() const = 0;
};

}  // namespace charon
