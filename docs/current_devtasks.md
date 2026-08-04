# Build order

The ordering rule: **nothing depends on something unproven.** Each phase ends
with a concrete, observable check. If that check does not pass, do not start
the next phase — every one of these bugs is cheaper to fix now than after it is
buried under three more layers.

Propellers stay **off** until Phase 11 says otherwise.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done

---

## Phase 0 — Repository scaffolding

- [ ] `[0.1]` Create `controller/` as a second PlatformIO project (`esp32dev`,
      Arduino, C++17 — mirror `firmware/platformio.ini`).
- [ ] `[0.2]` Create `shared/link/protocol.h` with the packet definitions from
      [communication.md](communication.md). Add `-I ../shared` to the
      `build_flags` of **both** projects so the framing is defined exactly once.
- [ ] `[0.3]` Add a `native` environment to `firmware/platformio.ini` for
      host-side unit tests, with a source filter that excludes hardware code.
- [ ] `[0.4]` Add Unity test scaffolding under `firmware/test/`. Get one
      trivial passing test running under `pio test -e native` before writing
      real ones.
- [ ] `[0.5]` Confirm `firmware/.pio/` is gitignored and no build artifacts are
      tracked.

**Check:** `pio run -e esp32dev` and `pio test -e native` both pass in
`firmware/`; `controller/` builds an empty sketch.

---

## Phase 1 — HAL and board bring-up

- [ ] `[1.1]` Write the pin map into `firmware/include/board/pins.h` as the
      single source of truth. Cross-check every pin against the strapping and
      input-only constraints in [hardware.md](hardware.md).
- [ ] `[1.2]` HAL wrappers: GPIO, SPI, I2C, LEDC/RMT, ADC1, `micros()`.
      Thin — no flight logic.
- [ ] `[1.3]` Serial console with a non-blocking print path. Nothing in the
      1 kHz loop may ever call it directly.
- [ ] `[1.4]` I2C bus scanner utility. You will use this constantly in Phase 2.

**Check:** blink an LED, print at 115200, scan the I2C bus and see devices.

---

## Phase 2 — Drivers

### 2a — IMU

- [ ] `[2.1]` `Icm20948Spi implements IImuSource`. Cut the SparkFun board's
      I2C/SPI jumper and wire SPI — see the rate argument in
      [hardware.md](hardware.md).
- [ ] `[2.2]` WHO_AM_I check, ranges (±2000 °/s gyro, ±8 g accel), DLPF config.
- [ ] `[2.3]` Data-ready interrupt → flag → read in the loop. Do **not** read
      inside the ISR.
- [ ] `[2.4]` Convert to SI units in the driver. Nothing downstream sees LSBs.
- [ ] `[2.5]` AK09916 magnetometer at ~100 Hz with its own valid flag.
- [ ] `[2.6]` Axis alignment: confirm +X forward, +Y right, +Z down. Rotate in
      the driver if the physical mounting differs. **Write down the convention
      and check it physically** — a swapped axis produces a control loop that
      is wrong in a way that looks like bad tuning.

**Check:** 1 kHz stream; gravity reads ≈9.81 on the down axis in all six
orientations; gyro reads ≈0 at rest and the correct sign when rotated.

### 2b — ToF pair

- [ ] `[2.7]` Wire both `XSHUT` pins to their own GPIOs.
- [ ] `[2.8]` Implement the boot address-assignment sequence from
      [hardware.md](hardware.md): both low → release A → set A to `0x30` →
      release B. **Verify with a bus scan** that exactly two devices answer at
      the expected addresses.
- [ ] `[2.9]` `Vl53l0xRanger` producing `RangeData`. Continuous mode,
      asynchronous — start ranging, poll for ready, never block.
- [ ] `[2.10]` Map range status codes to `valid`. Out-of-range must report
      invalid, not a large distance.
- [ ] `[2.11]` Measure and record: the baseline between sensors, each sensor's
      offset from the CG, and leg length at full extension. Into `Parameters`.

**Check:** both sensors report independently and correctly at a known height;
covering one does not disturb the other.

### 2c — Motors and battery

- [ ] `[2.12]` ESC driver: disarmed value at boot, arming sequence, normalized
      `0.0–1.0` input.
- [ ] `[2.13]` Motor test mode — spin one motor at a time from a serial
      command. **Props off.** Confirm the numbering and rotation direction of
      each motor against the mixer diagram in
      [control_loop.md](control_loop.md).
- [ ] `[2.14]` **Verify thrust direction physically** with props fitted, frame
      held down, one motor at a time: confirm each produces **upward** thrust.
      The props mount below the arms (pusher config), so either the prop
      handedness or the motor direction must be flipped relative to a
      top-mounted quad — one or the other, never both. Getting this wrong
      produces a vehicle that drives itself into the ground at full authority.
      Record the convention in [hardware.md](hardware.md).
- [ ] `[2.15]` Measure `leg_length`, `prop_bottom_offset`, and
      `prop_radius_from_CG`; compute the prop-strike tilt limit from the
      inequality in [hop_mode.md](hop_mode.md) and store all four in
      `Parameters`. This number sets `hop_max_tilt` later — it is not a
      comfort setting.
- [ ] `[2.16]` With motors spinning, confirm the ToF pair still reads cleanly.
      Props mounted low sit near the sensors' ~25° cone; blade intrusion shows
      up as spurious short readings at blade-pass frequency, which the
      estimator would read as a false ground contact.
- [ ] `[2.17]` Battery sense divider (see [hardware.md](hardware.md)) on ADC1;
      calibrate the reading against a meter.
- [ ] `[2.18]` Low-pass filter for battery voltage, plus the raw value for
      logging. Read the sag argument in [safety.md](safety.md) before
      choosing a threshold.

**Check:** each motor spins on command with correct numbering and direction;
thrust confirmed **upward** on all four; ToF clean with props spinning;
battery voltage matches a meter within 0.1 V.

---

## Phase 3 — Scheduler

- [ ] `[3.1]` 1 kHz fixed-rate task pinned to **core 1**.
- [ ] `[3.2]` Sub-rate dispatch by tick modulo, with staggered phases (see
      [scheduler.md](scheduler.md)).
- [ ] `[3.3]` Loop timing instrumentation: execution time and inter-tick
      jitter, min/mean/max.
- [ ] `[3.4]` Overrun detection feeding the watchdog failsafe.
- [ ] `[3.5]` Lock-free SPSC buffer for core 1 → core 0 handoff.

**Check:** with all Phase 2 drivers running, hot-path time stays under 400 µs
and jitter under 50 µs. Fix it here if not — everything downstream inherits it.

---

## Phase 4 — Estimator

- [ ] `[4.1]` Gyro bias calibration at rest during `INITIALIZING`, with a
      motion-detect reject.
- [ ] `[4.2]` Mahony attitude filter. Test on the host against synthetic
      samples first.
- [ ] `[4.3]` **Accelerometer trust gate** — reject the accel correction when
      `| ||a|| − 9.81 |` exceeds the band. This is what stops the attitude
      estimate tumbling during launch bursts and touchdown impacts. Non-optional
      for a hopcopter; see [estimator.md](estimator.md).
- [ ] `[4.4]` Magnetometer yaw trim at low gain, with magnitude rejection.
      Never let it touch roll or pitch.
- [ ] `[4.5]` Tilt-compensate each ToF range; invalidate beyond
      `tof_max_tilt`.
- [ ] `[4.6]` Cross-check the two heights; produce `height_confidence` and
      `ground_slope_rad`.
- [ ] `[4.7]` Height/vertical-velocity/accel-bias complementary filter:
      predict at 1 kHz, correct at 50 Hz.
- [ ] `[4.8]` Ground-contact detection: range **and** accel spike, latched with
      hysteresis.
- [ ] `[4.9]` Freshness timeouts and `healthy()` per the table in
      [estimator.md](estimator.md).

**Check:** tilt the frame by hand — roll/pitch track accurately in every
orientation and return cleanly to level with no drift. Shake it hard: attitude
must **not** tumble. Lift and lower it: height tracks; contact latches on the
bench and clears when lifted.

**Do not proceed until attitude is solid.** Everything after this is built on
it.

---

## Phase 5 — Link (serial first)

- [ ] `[5.1]` `ILink` interface: `open`, `send`, `poll`, `connected`,
      `last_rx_us`.
- [ ] `[5.2]` Frame encoder/decoder in `shared/link/`. Host-testable.
- [ ] `[5.3]` **Host unit tests for the parser**: truncated frames, bad CRC,
      reordered and replayed sequence numbers, garbage bytes mid-frame,
      resynchronization after corruption. Cheap tests, expensive bugs.
- [ ] `[5.4]` `SerialLink` over USB. Bring the whole stack up on this before
      touching ESP-NOW.
- [ ] `[5.5]` `TLM_STATE` telemetry at 10 Hz, queued to core 0.
- [ ] `[5.6]` Parameter get/set over the link, backed by NVS, with defaults,
      min/max, and range rejection.

**Check:** a host script reads live telemetry and changes a parameter that
persists across reboot.

---

## Phase 6 — Controller firmware (the transmitter)

- [ ] `[6.1]` Joystick ADC on **ADC1 only** (GPIO 36/39).
- [ ] `[6.2]` Calibration routine: record centre and both endpoints per axis,
      persist to NVS. Do not assume 2048 is centre.
- [ ] `[6.3]` Deadband around centre, normalize to −1000..1000, apply an
      expo curve for fine control near centre.
- [ ] `[6.4]` `SEL` button with `INPUT_PULLUP` and ~20 ms debounce.
- [ ] `[6.5]` Gesture recognizer: short click, long press 1.5 s, double click,
      3 s hold — per the table in [flight_modes.md](flight_modes.md).
- [ ] `[6.6]` Axis mode cycling `TILT → HEADING → ALTITUDE`, with local LED
      indication.
- [ ] `[6.7]` Emit `CMD_CONTROL` at 50 Hz **unconditionally**. The stream is
      the heartbeat; a gap in it is the failsafe signal.
- [ ] `[6.8]` Hold each `request` value ~100 ms so one dropped packet does not
      lose a button press.
- [ ] `[6.9]` Consume telemetry: LED and buzzer patterns for disarmed / armed /
      flying / hopping / failsafe / link-lost.

**Check:** with the controller on serial, every axis and gesture produces the
right packet. Verify the 3-second emergency-stop hold before anything can spin.

---

## Phase 7 — Ground station

Built here, before the control loop closes, because it is a **debugging
instrument** — it pays for itself across Phases 8–12. See
[ground_station.md](ground_station.md).

- [ ] `[7.1]` Controller forwards a combined record up the USB cable it is
      already plugged into for power: the vehicle's `TLM_STATE` plus its own
      stick position, axis mode, button state, and last gesture.
- [ ] `[7.2]` Host bridge process: read USB serial, decode using the
      definitions in `shared/link/`, re-serve over WebSocket. Python or Node —
      pick the one you'll actually maintain.
- [ ] `[7.3]` Bridge writes to disk **whether or not a browser is connected**.
      **One timestamped log file per arm/disarm cycle** — open on the
      `vehicle_state` transition into `ARMED`, close on the transition back to
      `DISARMED`. This is the black box for anything that goes wrong, and the
      per-flight boundary is what makes the blackbox tooling in Phase 13 able
      to reach for "the last flight" without slicing a continuous log.
- [ ] `[7.4]` Dashboard: vehicle state, per-failsafe tripped indicators,
      battery with thresholds marked, estimator health, `height_confidence`.
- [ ] `[7.5]` Dashboard: **which arming precondition is currently failing.**
      [safety.md](safety.md) already requires the vehicle to report this;
      this is where it becomes impossible to miss.
- [ ] `[7.6]` Rolling time-series plots — roll, pitch, yaw, height, vertical
      velocity. These matter more than a 3D view: oscillation and overshoot
      are obvious in a trace and invisible in an orientation cube.
- [ ] `[7.7]` Controller panel: stick position as a 2D dot with the deadband
      drawn, **post-calibration and post-expo** — the values as the firmware
      actually interpreted them.
- [ ] `[7.8]` Link quality: packets/sec, `rx_loss_pct`, CRC errors, sequence
      gaps, and time-since-last-command drawn against the 150 ms / 300 ms / 1 s
      failsafe ladder.
- [ ] `[7.9]` **Read-only for anything flight-critical.** Arming, mode changes,
      and emergency stop stay on the physical controller. Parameter tuning is
      the one exception, through the existing `CMD_PARAM_SET` rules.

**Check:** with the vehicle on the bench, tilt it by hand and watch attitude
track live in the browser; move the stick and watch the dot move; pull the
link and watch the failsafe ladder count up.

---

## Phase 8 — Modes, control, mixing

- [ ] `[8.1]` Vehicle state machine from [flight_modes.md](flight_modes.md),
      with all illegal transitions rejected. Host-tested.
- [ ] `[8.2]` Joystick → `DesiredState` mapping per axis mode. Unassigned axes
      hold their last value rather than snapping to zero.
- [ ] `[8.3]` Arming gesture with the **full precondition list** from
      [safety.md](safety.md), reporting which check failed.
- [ ] `[8.4]` Rate PID: D on measurement, low-pass filtered, clamped
      integrators.
- [ ] `[8.5]` Angle P loop feeding rate setpoints. **P only** — no I term here.
- [ ] `[8.6]` Yaw rate PI.
- [ ] `[8.7]` Altitude PID producing collective thrust; climb-rate command
      integrating into a held height.
- [ ] `[8.8]` Quad-X mixer with prioritized saturation handling
      (roll/pitch > thrust > yaw) and a `saturated` flag. **Verify the yaw
      signs independently** — the pusher prop mounting likely inverts them
      relative to the diagram in [control_loop.md](control_loop.md).
- [ ] `[8.9]` Anti-windup: freeze integrators whenever the mixer saturates.
- [ ] `[8.10]` **Freeze/bleed rate integrators while ground contact is
      latched**, reset at launch — otherwise every takeoff kicks.
- [ ] `[8.11]` Host unit tests: mixer allocation and saturation, PID step
      response, every state transition.

**Check (bench, props off, frame clamped):** tilt the frame by hand and confirm
the correct motors spool up to oppose the motion, on all three axes
independently. Watch it on the ground station. A sign error is obvious here and
unflyable later.

---

## Phase 9 — Safety

- [ ] `[9.1]` Safety module positioned **after** the mixer and before the motor
      write, holding the only path to the ESCs.
- [ ] `[9.2]` Failsafes 1–12 from [safety.md](safety.md).
- [ ] `[9.3]` Latching policy: estop and watchdog latch until power cycle;
      link-loss and battery clear as conditions but their landing completes.
- [ ] `[9.4]` Link-loss ladder: 150 ms warn → 300 ms failsafe → 1 s descend →
      disarm on touchdown.
- [ ] `[9.5]` Battery thresholds against the **filtered** voltage with a
      sustained-duration requirement, so launch bursts do not trip it.
- [ ] `[9.6]` Ring-buffer black box: every failsafe trip with timestamp and the
      state that caused it. Flush after landing, never in the hot path.
- [ ] `[9.7]` Emergency stop verified reachable from **every** state.

**Check:** deliberately trigger each failsafe on the bench — unplug the ToF
sensors, walk the controller out of range, hold the frame past 50°, drop the
supply voltage — and confirm the documented response every time, watching the
ground station's failsafe indicators.

---

## Phase 10 — ESP-NOW

- [ ] `[10.1]` `EspNowLink implements ILink`. No server/client asymmetry to
      design — both sides register the other as a peer and send.
- [ ] `[10.2]` `COMMAND` and `TELEMETRY` as fixed-size `esp_now_send()` frames;
      never block on the delivery callback before sending the next packet.
- [ ] `[10.3]` Peer binding: each side stores the other's MAC in NVS, registers
      only that MAC as a peer at boot, and rejects frames from any other
      source MAC. Explicit pairing gesture required to learn a new MAC.
- [ ] `[10.4]` Measure real round-trip latency and packet loss over range.
      `BleLink` is the documented fallback and `ILink` makes it a swap, not a
      rewrite, if ever needed.
- [ ] `[10.5]` Optional: per-peer AES-CTR encryption once the unencrypted link
      is proven.
- [ ] `[10.6]` Re-run the **entire** Phase 9 failsafe check over ESP-NOW. The
      link changed; the failsafes must be re-verified against it.

**Check:** full control over ESP-NOW with the vehicle tethered, props off.
Link loss at range produces the correct failsafe ladder.

---

## Phase 11 — First flight

Initial gain tuning happens on the rig, **not** in free flight. Free flight is
where you validate and fine-tune those rig-derived gains against real
aerodynamics — it is not where you discover them for the first time.

### 11a — Rig tuning

Props on, mounted on the PID tuning rack. The rig constrains translation but
allows free rotation about a fixed point, so the vehicle can actually execute
its PID corrections and you can watch real closed-loop response — overshoot,
oscillation, damping — with zero risk of a flyaway or a crash if a gain is
wrong. This is the right place to find out a gain is unstable, not hovering
freehand.

- [ ] `[11.1]` Mount on the rig. Confirm free rotation on all three axes with
      translation fully constrained before spinning a motor.
- [ ] `[11.2]` Tune the rate loop — P until oscillation, back off ~30%, then
      D, then I. One gain at a time, log everything via the ground station.
- [ ] `[11.3]` Tune the angle P loop: command a step tilt and confirm it
      reaches and holds the target angle without overshoot ringing into the
      rate loop.
- [ ] `[11.4]` Tune the yaw PI loop (`HEADING` mode) the same way.
- [ ] `[11.5]` Disturbance test: perturb the frame by hand on the rig, on each
      axis independently, and confirm it recovers to commanded attitude
      cleanly — this is your cheapest look at real damping before it matters
      in the air.

**Check:** on the rig, the vehicle holds a commanded tilt on all three axes,
recovers cleanly from a hand disturbance, and shows no D-term noise or
oscillation at the tuned gains.

**What the rig cannot tune:** it constrains translation, so it cannot
validate altitude hold, ground-effect response, or anything that depends on
the vehicle actually moving through air it disturbs itself. Those still need
free flight — the rig gets the rotational loops close, not complete.

### 11b — Free hover

Outdoors or a large open space, tether where practical, emergency stop under
your thumb, a spotter, and the ground station recording. Only after 11a's
check passes.

- [ ] `[11.6]` First free hover using the rig-tuned gains as the starting
      point. Expect only minor retuning — real prop wash, ground effect, and
      airframe asymmetry aren't present on the rig.
- [ ] `[11.7]` Hover trim: adjust until it holds level with a centred stick.
- [ ] `[11.8]` Fine-tune rate/angle gains in free air if the rig-derived
      values aren't quite right. One gain at a time, same discipline as 11a.
- [ ] `[11.9]` Tune altitude hold; verify climb-rate command and height hold.
      This loop gets its **first** real tuning here — the rig can't exercise
      it.
- [ ] `[11.10]` Verify `HEADING` mode yaw rate in free flight.
- [ ] `[11.11]` Verify each failsafe **in flight**, one at a time, at low
      altitude over something soft.

**Check:** stable hover, responsive tilt, altitude holds within ~10 cm, every
failsafe behaves as documented.

---

## Phase 12 — Hop mode

Only after Phase 11 is fully complete. Read [hop_mode.md](hop_mode.md) first —
the bring-up sequence there is the detailed version of this list.

- [ ] `[12.1]` Hop phase machine at 1 kHz, producing `HopState`.
- [ ] `[12.2]` Bottom-of-stroke detection: vertical velocity crossing zero
      while contact is latched.
- [ ] `[12.3]` Launch burst: `hop_burst_thrust` for `hop_burst_ms`, fired at
      extension.
- [ ] `[12.4]` Ballistic phase at `hop_idle_thrust` with **attitude control
      fully active**. Verify the mixer still has torque authority at idle
      collective — this is the failure mode that tumbles the vehicle.
- [ ] `[12.5]` Apex detection and peak-height capture, tolerating ToF dropout
      above ~1.2 m by coasting on the inertial filter.
- [ ] `[12.6]` Descent: tilt steering, pre-tilt to `ground_slope_rad`.
- [ ] `[12.7]` Closed-loop hop-height regulation — proportional, clamped,
      updated **once per hop**.
- [ ] `[12.8]` Every abort condition from [hop_mode.md](hop_mode.md).
- [ ] `[12.9]` `HOP_TOGGLE` double-click gesture wired end to end.
- [ ] `[12.10]` Hop phase timeline on the ground station, aligned against the
      height and vertical-velocity traces. This is the single most useful view
      for hop tuning — it is how you see whether the burst is firing at
      extension or missing the window.
- [ ] `[12.11]` High-rate hop logging to the ring buffer; flush after landing.
- [ ] `[12.12]` Tethered bring-up, steps 1–5 of
      [hop_mode.md](hop_mode.md#bring-up-order). Start the burst at 25 % and
      expect it not to leave the ground.

**Check:** repeatable hops at a commanded height, upright attitude through the
entire airborne phase, tilt steering the landing point, and every abort
condition verified.

---

## Phase 13 — Tooling and polish

- [ ] `[13.1]` Offline log decoder and hop-cycle analyzer over the bridge's
      session logs.
- [ ] `[13.2]` PID tuning helper — parameter sweeps driven from the ground
      station.
- [ ] `[13.3]` Endurance test: hopping vs hovering runtime on the same pack.
      This is the number the whole design exists to improve.
- [ ] `[13.4]` Record final tuned parameters and wiring in the docs.

---

## Standing rules

1. **Props off** until Phase 11 — the only exceptions before then are the
   thrust-direction check in `[2.14]` and the ToF-with-props check in
   `[2.16]`, both with the frame held down. Phase 11 itself starts on the PID
   tuning rack (`11a`, constrained rotation, no flyaway risk) before free
   hover (`11b`) is attempted.
2. **Never skip a phase check.** They are ordered so each failure is cheap and
   isolated.
3. **Attitude control never stops** — not in any hop phase, not ever.
4. **Log before you need it.** You get one flight's worth of evidence and no
   ability to reproduce a crash.
5. **One variable at a time** when tuning.
6. **Host-test everything that will compile on a host.** Mixer, PID, state
   machines, and the link parser are the cheapest bugs you will ever fix.
