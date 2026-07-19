# Estimator

## Purpose

Turn sensor data into the vehicle's best estimate of its own state
(`VehicleState`: orientation quaternion, roll/pitch/yaw, angular velocity,
and — later — altitude).

The estimator is the only module that knows how attitude is computed. The
Controller, Safety, and Telemetry modules consume `VehicleState` and never
learn where it came from.

## The core decision: abstract at the estimator output, not the IMU

Charon must run on two very different vehicles:

| | Hopcopter | Robosub |
|---|---|---|
| Sensor | Raw I2C IMU (accel + gyro) | VectorNav |
| Connection | Chip → ESP32 (I2C) | VectorNav → Jetson → ESP32 (USB) |
| Where fusion happens | **On the ESP32** | **Already done** by the VectorNav EKF |
| What the ESP32 receives | Raw accel/gyro | A fully-fused attitude solution |

These differ in **where sensor fusion happens**, not just in which chip is
used. That is why the abstraction seam is `IStateEstimator` → `VehicleState`,
**not** the raw IMU.

If we abstracted at the raw-IMU level instead, the Robosub path would have to
discard the VectorNav's EKF solution and re-run a crude Mahony filter on the
ESP32 — worse accuracy, added latency, and pointless, since the VectorNav's
filter is far better than anything the ESP32 can run.

## Interfaces

Two layered contracts, matching the Drivers-vs-Estimator split in
[architecture.md](architecture.md):

- **`IImuSource`** (driver layer, `firmware/include/drivers/IImuSource.h`) —
  raw accelerometer + gyroscope chips only. Produces `ImuData`.
  The VectorNav does **not** implement this.

- **`IStateEstimator`** (estimator layer,
  `firmware/include/estimator/IStateEstimator.h`) — produces `VehicleState`.
  This is the seam both vehicles satisfy.

## Implementations

```
Hopcopter:  I2cImu (IImuSource) ──► OnboardEstimator (Mahony) ──► VehicleState
Robosub:    JetsonLink ──► ExternalEstimator (passthrough)     ──► VehicleState
```

- **`OnboardEstimator`** — reads an `IImuSource`, runs onboard fusion
  (Mahony or Madgwick), fills `VehicleState`.
- **`ExternalEstimator`** — parses fused attitude packets from the Jetson USB
  link and copies the solution straight into `VehicleState`. No onboard fusion.

Selection happens once at boot via a factory keyed off a build flag
(`-D VEHICLE_HOPCOPTER` / `-D VEHICLE_ROBOSUB`) or a `Parameter`.

## Timing and health

- `update()` is **pull-based and must never block.** The fixed-rate loop calls
  it every tick.
- The I2C path delivers a fresh sample on (nearly) every tick. The Jetson path
  is **asynchronous** — packets arrive at the VectorNav/Jetson push rate
  (typically ~200–800 Hz), independent of the control loop. When no new sample
  is available, the estimator reuses its last `VehicleState`.
- Each estimator tracks `last_valid_us`. If
  `now_us - last_valid_us > freshness_timeout`, `healthy()` returns `false`,
  which trips the Safety module's sensor-timeout failsafe. Both vehicles use
  this same mechanism — a payoff of the shared interface.

## Link protocol (Robosub)

The Jetson → ESP32 attitude stream reuses the packet framing from
[communication.md](communication.md) (Header / Type / Length / Payload / CRC)
with a dedicated `ATTITUDE` packet type. One framing format for both the ground
link and the Jetson link is a deliberate simplification.

## Build order

Implement `OnboardEstimator` first and close the hopcopter control loop before
building `ExternalEstimator` / `JetsonLink`. The Robosub path reuses the same
`IStateEstimator` contract, so nothing is wasted by doing it second — and the
controller is proven before the Jetson link is added as a variable.
