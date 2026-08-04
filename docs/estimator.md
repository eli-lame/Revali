# Estimator

## Purpose

Turn raw sensor samples into `VehicleState`: the vehicle's best estimate of its
own attitude, angular rate, height above ground, vertical velocity, and whether
its foot is currently touching the ground.

The estimator is the only module that knows how any of this is computed. The
Controller, Hop Sequencer, Safety, and Telemetry consume `VehicleState` and
never learn where it came from.

## Inputs

| Source | Rate | Provides |
|---|---|---|
| ICM-20948 accel + gyro | 1 kHz | attitude, angular rate, vertical acceleration |
| ICM-20948 magnetometer | ~100 Hz | yaw reference (heading mode only) |
| VL53L0X A + B | ~50 Hz each | height, ground slope, touchdown |

## Attitude: Mahony

Use a Mahony complementary filter, not a Kalman filter. It is a handful of
lines, runs in single-precision in a few microseconds, and its two gains
(`Kp`, `Ki`) map onto behaviour you can reason about while tuning. On a vehicle
that spends part of every hop in free fall, filter *predictability* matters more
than optimality.

Two hopcopter-specific rules:

**Reject the accelerometer whenever it is not measuring gravity.** A Mahony
filter's accel correction assumes the only long-term acceleration is gravity.
On a hopcopter that assumption is violated hard and often: the launch burst and
the touchdown impact both produce multi-g spikes, and the ballistic phase
produces roughly *zero* g. Gate the correction on

```
| ||accel|| - 9.81 |  <  accel_trust_band     (start at 1.5 m/s²)
```

and when the gate fails, integrate gyro only. Without this gate the estimated
attitude will tumble at exactly the moment the vehicle most needs it — during
launch and landing.

**The magnetometer only trims yaw, and only slowly.** It is useless near the
motors under load. Feed it at low gain, reject samples whose magnitude deviates
from the calibrated field strength, and never let it touch roll or pitch. Roll
and pitch are what keep the vehicle alive; yaw drift is cosmetic by comparison.

Gyro bias must be estimated at rest during `INITIALIZING` — average several
hundred samples while the vehicle is still — and refined by the Mahony
integral term afterwards. Refuse to arm if the vehicle moved during
calibration.

## Height: two downward sensors

Each VL53L0X reports a slant range along its own boresight, not a vertical
height. Converting one into the other, and getting something useful out of
having two, is the whole job.

### Step 1 — tilt-compensate each range

```
h_i = d_i · cos(tilt) + z_offset_i
tilt = angle between body -Z and world -Z, from the attitude quaternion
```

`z_offset_i` accounts for the sensor sitting off the CG: a sensor mounted
forward of centre rides higher when the vehicle pitches up. At small angles
this is a minor correction; past ~20° it is not.

`cos(tilt)` blows up as tilt grows — at 60° the correction doubles the
measurement and doubles its noise with it. **Declare the range invalid beyond
`tof_max_tilt` (start at 40°)** and coast on inertial height instead.

### Step 2 — cross-check, then combine

With two tilt-compensated heights you can do something a single sensor cannot:
detect that one of them is lying.

```
if both valid and |h_A - h_B| < agreement_threshold:
        h = (h_A + h_B) / 2                      → high confidence
        slope = (h_A - h_B) / baseline           → ground plane tilt
else if exactly one valid:
        h = that one                             → degraded, flag it
else:
        h invalid                                → inertial coast only
```

A persistent disagreement means the vehicle is over an edge, a step, or an
object — or that a sensor has failed. Either way the height is not trustworthy
and Safety should know. Report the confidence level in `VehicleState`; do not
silently average two numbers that disagree.

The `slope` output is a genuine bonus of this mounting choice: it tells the
landing logic whether the ground under the foot is level, which matters because
a hopcopter landing on a slope needs to pre-tilt to keep the leg aligned with
the surface normal.

### Step 3 — fuse with the accelerometer

The ToF pair runs at ~50 Hz. The control loop runs at 1 kHz, and during the
ballistic phase the ground may be beyond the VL53L0X's usable range entirely
(spec 2 m, realistically ~1.2 m in daylight — and hops reach ~1.6 m). Height
therefore cannot come from ranging alone.

Run a small complementary or 3-state filter carrying **height, vertical
velocity, and accelerometer bias**:

- *Predict* at 1 kHz by integrating world-frame vertical acceleration
  (rotate body accel by the attitude quaternion, subtract gravity).
- *Correct* at ~50 Hz whenever a trustworthy tilt-compensated height arrives.

Vertical velocity is not optional. The hop controller needs it to detect apex
(`v_z` crosses zero) and to time the launch burst, and neither is recoverable
from a 50 Hz height signal alone.

When ranging drops out mid-hop, the filter coasts on inertial data. Bias
estimation is what makes that survivable for the ~0.6 s of a hop; without it,
double-integrated accel drift is metres per second squared of nonsense.

### Step 4 — ground contact

Touchdown detection drives the entire hop phase machine, so it must be fast
(sub-10 ms) and it must not false-trigger. Use two independent signals and
require agreement:

- **Range**: `h < leg_length + contact_margin` on at least one sensor.
- **Acceleration**: `||accel||` spike above `contact_accel_threshold`.

Range alone is too slow at 50 Hz — 20 ms of latency is most of a compression
stroke. Accel alone false-triggers on prop wash and hard steering. Together
they are reliable. Latch the result with a short hysteresis so a bouncing leg
does not produce a burst of contact/no-contact transitions.

## Health and freshness

Every source carries `last_valid_us`. The estimator reports `healthy() == false`
when:

| Condition | Timeout |
|---|---|
| No fresh IMU sample | 5 ms |
| No valid height while `FLYING` in an altitude-holding mode | 500 ms |
| Attitude uncertainty diverged (accel gated off too long) | 2 s |

`healthy() == false` trips Safety's sensor-timeout failsafe — see
[safety.md](safety.md). Note that losing the ToF pair is **not** fatal in
attitude-only modes; it is only fatal in modes that depend on height. Encode
that distinction rather than killing the vehicle for a lost range reading it
did not need.

## Timing contract

`update(now_us)` is pull-based and **must never block**. It is called every
tick by the fixed-rate loop. It returns `true` if fresh IMU data advanced the
state, `false` if it reused the previous estimate. It must never wait on an
I2C transaction — start VL53L0X ranging asynchronously and collect results
when the driver says they are ready.

## Build order

Attitude first, on the bench, with propellers off. Print roll/pitch/yaw and
tilt the frame by hand until the numbers are right in every orientation and
return to level cleanly. Only then add height. Only then add contact detection.

Do not attempt to debug attitude fusion and height fusion at the same time.
