# Flight modes

Two orthogonal things are called "mode" and confusing them will cost you a
vehicle. Keep them separate — and note they live on **different processors**:

- **Vehicle state** — where the vehicle is in its lifecycle. Controls whether
  motors may spin. Lives on the **vehicle**. Changed by Safety, arming, and
  failsafes.
- **Control scheme** — how a given input device maps its sticks/buttons onto
  the four intent fields of `CMD_CONTROL`. Lives entirely on the
  **controller**. The vehicle has no knowledge of it — it only ever receives
  resolved intent (desired roll/pitch/yaw-rate/climb-rate). A control scheme
  **never affects whether the vehicle is armed.**

The rest of this document is split accordingly: the vehicle state machine is
firmware that runs on the aircraft; control schemes are firmware that runs on
whichever controller you built.

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
- controller reports neutral input (`CMD_CONTROL.flags` known-neutral bit set,
  and the four command fields at zero)
- link alive, receiving fresh commands
- battery above `arm_min_voltage`
- no latched fault

## Control schemes (controller-side)

Every controller must produce the same four intent fields —
`roll_cmd`, `pitch_cmd`, `yaw_rate_cmd`, `climb_rate_cmd` — plus a `request`.
*How* it produces them depends on how many axes and buttons it has. This is
the device-specific logic that the intent-packet design deliberately keeps off
the vehicle. Two reference schemes:

### Two-stick gamepad (DS4 / SCUF) — no modes at all

A gamepad has four analog axes, so it fills all four intent fields **directly
and simultaneously**. There is nothing to cycle. Self-centring sticks map
perfectly onto the intent semantics: release the right stick and both
`yaw_rate_cmd` and `climb_rate_cmd` return to zero — *hold heading, hold
height*. Release the left stick and the vehicle levels. No mode, no LED to
read. This is the ergonomic payoff of the extra axes, and why a gamepad is
worth supporting.

Full reference mapping:

| Input | Produces | Notes |
|---|---|---|
| **Left stick** X / Y | `roll_cmd` / `pitch_cmd` | tilt / translation |
| **Right stick** X / Y | `yaw_rate_cmd` / `climb_rate_cmd` | centered = hold heading & height |
| **✕ (Cross)** | `request = LAND` | bottom button = "down"; the everyday bring-it-home |
| **○ (Circle)** | `request = SET_HOP` | enter hopping |
| **△ (Triangle)** | `request = SET_HOVER` | back to hover / normal flight |
| **□ (Square)** | *reserved* | candidate: "level now" gentle recovery |
| **D-pad ↑ / ↓** | `hop_height_target` +/− | via `CMD_PARAM_SET`, not a request |
| **D-pad ← / →** | *reserved* | candidate: roll/pitch trim |
| **L2 (hold)** | precision mode | scales all four intent fields to ~40% |
| **L1 + R1 (hold 1.5 s)** | `request = ARM` / `DISARM` | two-handed, deliberate; DISARM only on ground |
| **Touchpad (hold ~2 s)** | `request = ESTOP` | distinctive, deliberate, panic-reachable |
| **Share** | log marker | bookmark the blackbox for later analysis |
| **L3 / R3** (stick clicks) | *unused* | too easy to hit by accident |

Three rules behind the choices, each a safety property, not an aesthetic:

- **`LAND` is a single, easy button; `ESTOP` is a deliberate hold.** `LAND`
  (controlled descent) covers almost every "get it down now" moment, so it
  earns the easiest-to-reach button. `ESTOP` cuts motors and the vehicle
  *drops* — it is the nuclear option for "uncontrollable / about to hurt
  someone," and it should be hard to trigger by accident. The touchpad hold
  gives both: nothing rests on the touchpad, yet it is a big target you can
  slam and hold in a panic.
- **Arming takes two hands.** `L1 + R1` held together can't happen from a
  bumped button, and spinning motors should never be one careless press away.
- **Precision, trim, and rate scaling never touch the vehicle.** They shape the
  intent *before* it is sent — the vehicle just receives smaller or offset
  command values and clamps them as always. Only `hop_height_target` changes
  vehicle behavior, and it reuses the parameter path.

There is no gesture vocabulary to debounce here — dedicated buttons replace the
one-button device's click/hold language entirely.

### One-stick joystick (the reference controller) — modes by necessity

The reference controller has one two-axis stick, which can express only two of
the four intent fields at once. It resolves the other two by cycling an
internal **control-scheme mode** with its single button. **Roll is always
roll** — stick X is `roll_cmd` in every mode — so lateral correction is never
lost. Only stick Y is reassigned:

| Scheme | Stick X → | Stick Y → | LED |
|---|---|---|---|
| `TILT` (default) | `roll_cmd` | `pitch_cmd` | solid |
| `HEADING` | `roll_cmd` | `yaw_rate_cmd` | slow blink |
| `ALTITUDE` | `roll_cmd` | `climb_rate_cmd` | fast blink |

The fields not driven by the current scheme are filled with their **hold
value**, not zero: `pitch_cmd` retains its last commanded tilt when you leave
`TILT` (switching schemes should not instantly level a deliberate pitch),
while `yaw_rate_cmd` and `climb_rate_cmd` naturally rest at zero (hold
heading / hold height) whenever their scheme is not selected. All of this is
the *controller's* bookkeeping — it ships resolved intent every tick and the
vehicle is none the wiser.

This mode-cycling exists **only** because a one-stick device is axis-starved.
It is not a vehicle concept, and the gamepad above does not have it.

The joystick's single `SEL` button carries the whole request vocabulary as a
gesture language (debounce ~20 ms):

| Gesture | Timing | Produces | Valid in |
|---|---|---|---|
| Short click | < 400 ms | cycle control scheme | any flying state |
| Long press | 1.5 s | `ARM` / `DISARM` / `LAND` (per state) | see below |
| Double click | 2 clicks < 400 ms | hop toggle → resolves to `SET_HOP` or `SET_HOVER` | `FLYING` / `HOPPING` |
| Hold | 3 s | `ESTOP` | any state, always |

The double-click is a *toggle gesture*, but it never sends a toggle: the
controller reads the vehicle's current state from `TLM_STATE` and emits
`SET_HOP` when flying or `SET_HOVER` when hopping. The wire only ever sees the
explicit, idempotent request.

## Requests and the emergency stop (both controllers)

However a controller generates them, `request` values obey the same rules:

- **Emergency stop is always available and always wins.** It is checked before
  any other request and is honored in every state including `FAILSAFE`. On the
  joystick it is the 3-second `SEL` hold; on the gamepad it is the ~2-second
  touchpad hold. Whatever the input, it must be hard to trigger by accident and
  impossible to miss on purpose.
- **Disarm in flight is not a disarm.** A disarm request while airborne starts
  a controlled landing. Cutting motors at altitude is the separate, deliberately
  hard `ESTOP` action, never a side effect of asking to disarm.

Every request is *just a request*. The vehicle validates each against its own
state and silently ignores illegal ones — an arm request from a tilted vehicle
is dropped, not obeyed. This is unchanged regardless of which controller
produced it.

## Failsafe and pilot input

During `FAILSAFE` the vehicle **ignores incoming intent entirely** and runs its
own descent. Whatever control scheme the controller is in is irrelevant — the
vehicle isn't reading the command fields. (Emergency stop is the sole exception
that still gets through.)

On recovery from a link-loss failsafe, the vehicle does **not** silently hand
control back mid-descent. It completes the landing. The pilot re-arms. This is
the boring choice and it is the right one: a link that dropped once will drop
again, and handing back control to a pilot who has lost situational awareness
is worse than a completed landing.
