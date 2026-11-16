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

## IMU full-scale ranges

**Gyro: ±2000 dps. Accel: ±16 g.** Not the ICM-20948's power-on defaults of
±250 dps and ±2 g, which are far too narrow for this vehicle.

This is measured, not assumed. The bring-up self-test
(`firmware/src/tools/imu_selftest.cpp`) runs at the power-on defaults, and
**casual rotation by hand saturates the gyro** — it pins at ±250.13 dps, the
exact `32767 / 131` full-scale rail:

```
g[ -250.14  -250.14    44.37] deg/s
```

If a hand twist rails it, a touchdown impact transmitted through a sprung leg
certainly will. A saturated gyro does not report a large rate; it reports a
*wrong* one, and it does so precisely during launch and landing — the moments
the accel gate above has already rejected the accelerometer, leaving the gyro
as the only attitude source. That combination violates standing rule 4
(attitude control never stops), so the wide range is not optional.

The cost is resolution: at ±2000 dps the gyro reads 16.4 LSB/(deg/s) instead
of 131, so one LSB is 0.061 deg/s rather than 0.0076. That is far below both
the part's noise floor and anything the controller can act on. Saturation is
a correctness failure; resolution here is not.

Changing the range changes the conversion constants with it — see
`kGyroLsbPerDps` and `kAccelLsbPerG` in the self-test, which must track
whatever bank 2 is configured to.

## Accelerometer calibration

The accel magnitude is not 9.81 out of the box. A bench-measured part read
**|a| = 10.07 m/s² at rest**, about 2.7% high — ordinary scale-factor and
zero-g offset tolerance, not a wiring fault.

This matters more than it looks, because the accel trust band above is
`1.5 m/s²`: an uncorrected 0.26 m/s² magnitude error consumes roughly a sixth
of the band before the vehicle has moved at all, and it does so asymmetrically
depending on orientation. Calibrate scale and offset per axis (a six-position
static calibration is sufficient) and store the result in `Parameters`.

Attitude itself is tolerant of this — Mahony uses the *direction* of the accel
vector, which a common scale error does not rotate. It is the gravity-vs-hop
discrimination that needs the magnitude to be right.

## VL53L0X measured behaviour

Bench-measured on the Stage 2 board with `firmware/src/tools/tof_selftest.cpp`,
ToF A against a flat wall, square-on, indoors.

**Run the fast profile: 20 ms timing budget, ~50 Hz, signal rate limit
0.25 MCPS.** The long-range profile (50 ms, 0.10 MCPS, lengthened VCSEL
periods) is not needed. Fast tracks cleanly past 700 mm with no dropouts,
so the sample rate costs nothing to keep.

Noise, from settled static segments:

| Distance | 1σ | Relative |
|---|---|---|
| 300 mm | ~5 mm | 1.8% |
| 515 mm | ~5 mm | 1.0% |
| 675 mm | ~17 mm | 2.5% |

Noise grows with range, roughly 1–2.5% over the measured span. At a 500 mm
hop apex expect about ±10 mm of per-sample noise on each sensor — well
inside what the two-sensor fit and filtering absorb, but not negligible when
setting touchdown thresholds.

The ceiling was not reached; it is above 700 mm. If a hop profile ever needs
more, re-measure before assuming.

### The validity gate needs hysteresis, not a single-sample test

Near the limit, validity *flickers*: isolated `out of range` samples appear
among perfectly good readings rather than the sensor cutting out cleanly.
A gate that drops the sensor on one bad sample will drop it constantly at
the edge of range.

Require N consecutive invalid samples before declaring the sensor lost, and
N consecutive valid ones before trusting it again.

### The two sensors are not sampled simultaneously

A and B are separate I2C devices, read one after the other, and each read
blocks until that sensor has a fresh sample. The two readings in a "pair"
can therefore be tens of milliseconds apart.

Standing still this is invisible. In flight it is not: at a vertical speed
of 2 m/s, a 20 ms skew puts **40 mm** of difference between the two
readings with no ground slope present at all. The estimator cannot tell
that apart from a real 40 mm slope, and it appears exactly during the fast
vertical phases where the accel gate has already rejected the
accelerometer.

So the driver must **timestamp each sensor's sample individually** and
expose both timestamps, rather than handing the estimator a pair and
implying they are simultaneous. The slope fit then corrects for the age
difference using the current vertical velocity estimate.

This is a property of reading two independent sensors over one bus, not a
wiring fault, and it does not go away with better mounting.

### Two sample classes the driver must reject outright

Both were observed passing through as structurally valid readings:

- **Sub-minimum values.** A `6 mm` reading appeared between out-of-range
  samples. The part's floor is ~30 mm; anything below that is a spurious
  return, not a close target.
- **Isolated jumps.** A single `656 mm` arrived immediately before a long
  dropout run, surrounded by unrelated values. One sample inconsistent with
  both its neighbours is noise, and on a vehicle moving at hop speeds the
  rate of change is bounded — use that bound.

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
