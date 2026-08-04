# Flight modes

Two orthogonal things are called "mode" and confusing them will cost you a
vehicle. Keep them separate in code:

- **Vehicle state** — where the vehicle is in its lifecycle. Controls whether
  motors may spin. Changed by Safety, arming, and failsafes.
- **Axis mode** — what the joystick's two axes currently mean. Purely an input
  mapping. Changed by the pilot clicking the joystick button. **Never affects
  whether the vehicle is armed.**

## Vehicle state machine

```
              BOOT
                │  hardware init
                ▼
          INITIALIZING ──────────────┐
                │  gyro bias, sensor │ any check fails
                │  checks, link up   │
                ▼                    ▼
           DISARMED  ◄────────────  FAULT
                │  ▲                     (requires power cycle)
     arm gesture│  │ disarm gesture
                ▼  │ or landed+idle
             ARMED ─┤
                │  ▲
   thrust raised│  │
                ▼  │
             FLYING ──────► HOPPING ──────┐
                │  ▲          │           │
                │  └──────────┘           │
        land cmd│                         │
                ▼                         │
             LANDING ──────► DISARMED     │
                                          │
   any state ──► FAILSAFE ◄───────────────┘
                    │  recoverable: auto-land
                    │  unrecoverable
                    ▼
              EMERGENCY_STOP  (motors zero, latched)
```

### What each state permits

| State | Motors | Accepts pilot input | Exit |
|---|---|---|---|
| `BOOT` | off | no | automatic |
| `INITIALIZING` | off | no | automatic, or → `FAULT` |
| `FAULT` | off | no | **power cycle only** |
| `DISARMED` | off | no | arm gesture |
| `ARMED` | idle spin | no | thrust ramp → `FLYING`, or disarm |
| `FLYING` | live | yes | `HOPPING`, `LANDING`, `FAILSAFE` |
| `HOPPING` | live, pulsed | tilt only | `FLYING`, `LANDING`, `FAILSAFE` |
| `LANDING` | descending | tilt only | `DISARMED` on touchdown |
| `FAILSAFE` | controlled descent | **no** | `DISARMED` on touchdown |
| `EMERGENCY_STOP` | zero, latched | no | power cycle only |

`ARMED` deliberately spins the motors at idle. It is unambiguous feedback to
everyone nearby that the vehicle is live, and it confirms all four ESCs armed
before any thrust is commanded.

**Arming preconditions** — all must hold, no exceptions:

- estimator healthy, attitude valid, gyro bias calibrated
- vehicle within `arm_max_tilt` of level (start at 10°)
- joystick centred within deadband
- link alive, receiving fresh commands
- battery above `arm_min_voltage`
- no latched fault

## Axis modes

One button, so the mapping cycles. **Roll is always roll** — the X axis
commands roll tilt in every mode, so the pilot never loses the ability to
correct a lateral drift. Only the Y axis is reassigned.

| Axis mode | X axis | Y axis | LED |
|---|---|---|---|
| `TILT` (default) | roll angle | pitch angle | solid |
| `HEADING` | roll angle | yaw **rate** | slow blink |
| `ALTITUDE` | roll angle | climb **rate** | fast blink |

Short click cycles `TILT → HEADING → ALTITUDE → TILT`.

Rationale for the defaults:

- **`TILT`** is the primary mode and the one that matters for hopping. Both
  axes command a tilt *angle*, which is what actually steers a hopcopter:
  tilting during the airborne phase changes where it lands.
- **`HEADING`** trades pitch for yaw rate so you can turn to face a direction.
  Yaw is commanded as a *rate*, not an angle — absolute heading hold depends
  on the magnetometer, which is unreliable near loaded motors.
- **`ALTITUDE`** trades pitch for climb rate, letting you retrim the hold
  height. This is the only way to change altitude, and it commands a **rate**,
  never raw throttle. Releasing to centre holds the new height. The joystick
  never has direct authority over collective thrust.

Whatever axis is not assigned in the current mode **holds its last commanded
value**, it does not snap to zero. Switching out of `TILT` should not
instantly level a deliberate pitch attitude.

## Button gestures

The single `SEL` switch carries the full input vocabulary, so gesture
separation must be unambiguous. Debounce ~20 ms, then:

| Gesture | Timing | Action | Valid in |
|---|---|---|---|
| Short click | < 400 ms | cycle axis mode | any flying state |
| Long press | 1.5 s | **arm** (with tone/LED confirm) | `DISARMED` only |
| Long press | 1.5 s | **disarm** | `ARMED` on ground |
| Long press | 1.5 s | **initiate landing** | `FLYING`, `HOPPING` |
| Double click | 2 clicks < 400 ms | toggle `HOPPING` | `FLYING` only |
| Hold | 3 s | **emergency stop** | any state, always |

Two rules that are not negotiable:

1. **Emergency stop is always available and always wins.** It is checked
   before any other gesture and works in every state including `FAILSAFE`.
2. **Disarm in flight is not a disarm.** A long press while airborne starts a
   controlled landing. Cutting motors at altitude is what the 3-second hold is
   for, and it should feel deliberately hard to trigger by accident.

Gesture recognition runs on the **controller**, but the resulting request is
just a request. The vehicle validates every one against its own state and
silently ignores illegal ones — an arm request from a tilted vehicle is
dropped, not obeyed.

## Mode changes and failsafe

A failsafe **overrides axis mode entirely**. During `FAILSAFE` the vehicle
ignores pilot input and runs its own descent. Axis mode is preserved and
restored if the failsafe clears, but it grants no authority while active.

On recovery from a link-loss failsafe, the vehicle does **not** silently hand
control back mid-descent. It completes the landing. The pilot re-arms. This is
the boring choice and it is the right one: a link that dropped once will drop
again, and handing back control to a pilot who has lost situational awareness
is worse than a completed landing.
