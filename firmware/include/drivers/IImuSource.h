#pragma once
//
// IImuSource — driver-layer contract for a RAW inertial sensor.
//
// Scope: chips that report raw accelerometer + gyroscope samples and rely on
// THIS firmware to fuse them into an attitude. The hopcopter's sensor is a
// SparkFun ICM-20948 on I2C.
//
// The interface exists so the fusion filter can be unit-tested against a
// recorded or synthetic sample stream with no hardware attached, and so a
// bench-test build can swap in a fake IMU. See docs/estimator.md.

#include "core/types.h"

namespace revali {

class IImuSource {
public:
    virtual ~IImuSource() = default;

    // Bring the sensor up (bus config, WHO_AM_I check, ranges, etc.).
    // Returns false if the device is absent or misconfigured.
    virtual bool begin() = 0;

    // Read the latest sample. Returns false (and leaves `out.valid == false`)
    // if the read failed; callers must treat that as a sensor timeout.
    virtual bool read(ImuData& out) = 0;

    // True once begin() has succeeded and recent reads are healthy. Feeds
    // Safety's sensor-timeout logic.
    virtual bool healthy() const = 0;
};

}  // namespace revali
