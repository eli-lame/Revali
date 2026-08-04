# Data model

The structs in `firmware/include/core/types.h` are the shared vocabulary
between modules. They carry **no logic and no hardware knowledge** — plain data
only. Every module compiles against that header, so changing a contract here is
a deliberate, cross-cutting decision.

Rules that keep this useful:

- Plain, trivially-copyable aggregates. No virtuals, no heap, no `std::string`.
- Explicit units in every field name or comment. `_m`, `_rad`, `_us`, `_v`.
- Every sample carries a `timestamp_us` and a `valid` flag. Consumers check
  both. An unchecked stale sample is indistinguishable from a fresh one, and
  that is how a vehicle flies into the ground on a reading from two seconds ago.
- Body frame unless a field says otherwise. Say otherwise loudly.

## Types

### `ImuData` — owner: IMU driver, consumer: estimator

Raw ICM-20948 sample.

| Field | Unit | Note |
|---|---|---|
| `timestamp_us` | µs | `micros()` at sample time, not at read time |
| `accel` | m/s² | body frame |
| `gyro` | rad/s | body frame |
| `mag` | µT | body frame; updates ~10× slower than accel/gyro |
| `mag_valid` | — | separate flag because of the rate mismatch |
| `temperature_c` | °C | optional |
| `valid` | — | false ⇒ failed read, treat as a timeout |

### `RangeData` — owner: ToF driver, consumer: estimator

One VL53L0X measurement. Two instances exist.

| Field | Unit | Note |
|---|---|---|
| `timestamp_us` | µs | |
| `distance_m` | m | **along the sensor boresight, not vertical** |
| `valid` | — | false ⇒ out of range, bad status, or stale |

The distinction between slant range and height is the source of most bugs in
this subsystem. The driver reports what the sensor measured; only the estimator
applies tilt compensation. See [estimator.md](estimator.md).

### `VehicleState` — owner: estimator, consumers: controller, hop sequencer, safety, telemetry

The single seam between sensing and control.

| Field | Unit | Note |
|---|---|---|
| `timestamp_us` | µs | time the estimate is valid for |
| `orientation` | quat | **authoritative** |
| `roll_rad`, `pitch_rad`, `yaw_rad` | rad | derived, for logging and limits |
| `angular_velocity` | rad/s | body frame, bias-corrected |
| `height_m` | m | above ground, tilt-compensated |
| `vertical_velocity_ms` | m/s | fused; required for apex detection |
| `ground_slope_rad` | rad | from the ToF pair's disagreement |
| `height_confidence` | enum | `NONE` / `INERTIAL` / `SINGLE` / `BOTH` |
| `ground_contact` | — | latched, debounced |
| `valid` | — | false ⇒ do not act on this |

`height_confidence` exists so consumers can make different decisions with
degraded data. Altitude hold needs `BOTH` or `SINGLE`; a hop can coast on
`INERTIAL` for a fraction of a second; nothing may act on `NONE`. Collapsing
this to a single boolean throws away the distinction that keeps a lost range
reading from becoming an emergency.

### `DesiredState` — owner: mode manager, consumer: controller

| Field | Unit | Note |
|---|---|---|
| `timestamp_us` | µs | |
| `roll_rad`, `pitch_rad` | rad | tilt setpoints from the joystick |
| `yaw_rate_rads` | rad/s | **rate**, not angle |
| `climb_rate_ms` | m/s | in `ALTITUDE` axis mode |
| `height_m` | m | hold target, integrated from climb rate |
| `thrust` | 0..1 | collective; **overridden by the hop sequencer** |
| `use_height_hold` | — | false in attitude-only modes |

Yaw is a rate, not an angle, because absolute heading depends on a
magnetometer that is unreliable near loaded motors. Altitude is commanded as a
rate that integrates into a held height, which is what lets a two-axis joystick
retrim altitude without ever having direct throttle authority.

### `ControlOutput` — owner: controller, consumer: mixer

| Field | Unit |
|---|---|
| `timestamp_us` | µs |
| `torque` | normalized −1..1 per axis |
| `thrust` | normalized 0..1 collective |

### `MotorOutput` — owner: mixer, consumers: safety then motor manager

| Field | Note |
|---|---|
| `timestamp_us` | |
| `count` | 4 |
| `value[4]` | normalized 0..1 |
| `saturated` | mixer hit a limit — controller freezes integrators |
| `armed` | |

`saturated` travels with the output because the controller needs it on the
*next* tick to suppress windup. Recomputing it elsewhere would duplicate the
mixer's allocation logic.

### `BatteryState` — owner: battery driver, consumers: safety, telemetry

| Field | Unit | Note |
|---|---|---|
| `timestamp_us` | µs | |
| `voltage_v` | V | **filtered** — raw sags hard on every launch burst |
| `voltage_raw_v` | V | unfiltered, logging only |
| `current_a` | A | 0 if not measured |
| `valid` | — | |

### `HopState` — owner: hop sequencer, consumers: controller, telemetry, logging

| Field | Note |
|---|---|
| `phase` | `STANCE` / `LAUNCH` / `ASCENT` / `APEX` / `DESCENT` |
| `phase_entered_us` | for timeouts and burst duration |
| `last_apex_height_m` | feeds height regulation |
| `hop_count` | |
| `abort_reason` | why the last hop ended early |

### `LinkStatus` — owner: link, consumers: safety, telemetry

| Field | Note |
|---|---|
| `last_rx_us` | the failsafe input |
| `connected` | |
| `rx_count`, `crc_errors`, `seq_gaps` | link quality |

### `Parameter`

Persisted in NVS: PID gains, hop timing and thrust, ToF geometry and leg
length, joystick calibration, failsafe thresholds, bound controller MAC.

Every parameter needs a compiled-in default, a min, and a max. Reject
out-of-range writes rather than clamping silently — a gain that got clamped
without telling you is a tuning session spent chasing nothing. Refuse writes
while armed except for a small explicit in-flight allowlist.
