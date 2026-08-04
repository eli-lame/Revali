# Safety

Safety is a module, not a habit. It sits downstream of the controller and mixer
and holds a **veto over motor output**. Nothing else in the firmware writes to
the ESCs.

```
Controller → Mixer → MotorOutput ──► Safety ──► Motor Manager ──► ESCs
                                       ▲
                    estimator health ──┤
                       link freshness ─┤
                              battery ─┤
                             watchdog ─┤
                          pilot ESTOP ─┘
```

Safety runs **every tick**, at the full 1 kHz loop rate, after the mixer and
before the motor write. It is the last thing to touch the output.

## Failsafes

| # | Failsafe | Trips when | Response |
|---|---|---|---|
| 1 | **Emergency stop** | pilot 3 s hold, any state | motors → 0, **latched**, power cycle to clear |
| 2 | **Link loss** | no valid command 300 ms | level, hold height, descend at 1 s |
| 3 | **Sensor timeout** | no IMU 5 ms; no height 500 ms in height-dependent mode | IMU → estop; height → drop to attitude-only |
| 4 | **Attitude limit** | tilt > `max_tilt` (start 50°) | level command, cannot be overridden by pilot |
| 5 | **Battery low** | < `warn_voltage` | warn; force land at `land_voltage`; estop at `critical_voltage` |
| 6 | **Watchdog** | loop overrun > 2 ticks, or task starved | estop |
| 7 | **Height ceiling** | height > `max_height` | clamp climb rate to zero |
| 8 | **Ground contact when not expected** | contact during `FLYING` above idle | cut to idle — likely a crash |
| 9 | **Arming interlock** | any precondition unmet | refuse to arm, report reason |
| 10 | **Boot lockout** | outputs before init complete | hold disarmed value until Safety releases |
| 11 | **ESC health** | any output channel not confirmed at arm | refuse to arm |
| 12 | **Estimator divergence** | accel gated off > 2 s, attitude untrusted | estop |
| 13 | **Hop abort** | see [hop_mode.md](hop_mode.md) | → `FLYING`, level |

## Rules

**Failsafes latch by severity, not uniformly.** Emergency stop and watchdog
latch until power cycle. Link loss and low battery are *conditions* — they
clear when the condition clears, but the resulting landing still completes.
Never auto-resume flight mid-descent; make the pilot re-arm. A pilot who has
lost the link has also lost situational awareness.

**Battery voltage must be filtered and load-compensated.** Motor bursts sag the
pack hard, and a hopcopter's launch bursts sag it harder than a hover ever
does. An unfiltered reading will trip the low-battery failsafe during every
single launch. Low-pass the measurement and evaluate the threshold against the
filtered value, with a sustained-duration requirement (e.g. below threshold for
1 s continuously) before tripping.

**The disarmed value is not zero.** ESCs need a valid idle signal to stay armed
and responsive. Boot with outputs at the disarmed pulse width and hold there.
Zero output means an unarmed ESC that will not respond when you need it.
Emergency stop is the exception — that one really does go to zero.

**Safety must not allocate, block, or log to serial in its hot path.** It runs
in the 1 kHz loop. Set a flag; let the 10 Hz task print it.

**Every trip is recorded.** Which failsafe, at what timestamp, with the state
that caused it, into the ring buffer. When something goes wrong you will have
exactly one flight's worth of evidence and no ability to reproduce it.

## Arming

Arming is the highest-consequence transition in the firmware. All of these must
hold, checked fresh at the moment of the request:

- [ ] estimator healthy, attitude valid
- [ ] gyro bias calibrated, and the vehicle did not move during calibration
- [ ] tilt within `arm_max_tilt` (10°)
- [ ] joystick centred within deadband, and the controller agrees it is centred
- [ ] link alive with fresh commands
- [ ] battery above `arm_min_voltage`
- [ ] no latched fault
- [ ] all four ESC outputs initialized

Report *which* check failed over telemetry. "Won't arm" with no reason is the
most frustrating possible failure mode, and you will hit it often.

Arming spins motors to idle deliberately — audible, visible confirmation that
the vehicle is live.

## Bench discipline

The firmware cannot enforce these. Do them anyway.

- **Propellers off** for every step until the task list explicitly says
  otherwise. Most of the stack can be validated without them.
- **Clamp the frame** for the props-on sign/direction checks in Phase 2. For
  first closed-loop tuning (Phase 11a), use the PID tuning rack instead of an
  improvised tether — it constrains translation while letting the vehicle
  actually execute its corrections, so a bad gain produces visible bad
  behavior on a fixed mount instead of a flyaway.
- **Emergency stop under your thumb**, tested *before* every powered run — not
  after something goes wrong.
- **Current-limited supply** instead of a LiPo during bring-up where possible.
  It turns a wiring mistake into a shrug.
- Know where the battery disconnect is.
