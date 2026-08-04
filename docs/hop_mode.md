# Hop mode

## The idea

The leg is passive: a rigid upper section, a sliding lower rod, and rubber
bands. It has no actuator. It cannot decide to jump. **Every bit of control
authority comes from the four rotors**, and the firmware's entire contribution
to hopping is *timing thrust correctly relative to ground contact*.

Why bother: a hover burns thrust continuously. A hop stores energy in the
spring, releases it in a burst, and then coasts ballistically with the rotors
nearly off. The reference vehicle went from ~6 minutes of hover endurance to
~50 minutes of hopping on the same battery. The efficiency comes entirely from
the rotors being *off* for most of the cycle.

## Phase machine

Runs at the full 1 kHz loop rate — touchdown must be caught within a few
milliseconds or the launch burst lands in the wrong part of the stroke.

```
                  ┌──────────────────────────────────────────┐
                  ▼                                          │
   ┌────────────────────────┐                                │
   │  STANCE                │  foot down, spring compressing │
   │  thrust: attitude only │                                │
   └───────────┬────────────┘                                │
               │  compression bottoms out (v_z ≈ 0, contact) │
               ▼                                             │
   ┌────────────────────────┐                                │
   │  LAUNCH                │  spring releasing              │
   │  thrust: BURST         │  add energy in phase w/ spring │
   └───────────┬────────────┘                                │
               │  contact lost                               │
               ▼                                             │
   ┌────────────────────────┐                                │
   │  ASCENT                │  ballistic, rotors near idle   │
   │  thrust: idle          │  attitude control STILL ACTIVE │
   └───────────┬────────────┘                                │
               │  v_z crosses zero                           │
               ▼                                             │
   ┌────────────────────────┐                                │
   │  APEX                  │  measure peak height           │
   │  thrust: idle          │  → feeds burst regulation      │
   └───────────┬────────────┘                                │
               │                                             │
               ▼                                             │
   ┌────────────────────────┐                                │
   │  DESCENT               │  steer landing point by tilt   │
   │  thrust: idle + trim   │  pre-tilt to ground slope      │
   └───────────┬────────────┘                                │
               │  ground contact detected ───────────────────┘
               ▼
        (or → ABORT on any fault → FLYING → LANDING)
```

## What the pilot controls

Nothing about the hop cycle itself. The joystick commands **tilt**, exactly as
in normal flight, and tilt during `ASCENT`/`DESCENT` is what steers where the
next landing happens. That is the whole interface, and it is why "joystick
controls tilt only" is the right constraint rather than a limitation.

Hop height is a parameter, regulated onboard. The pilot toggles hopping on and
off with a double click and steers with tilt.

## Thrust profile

| Phase | Collective thrust | Attitude control |
|---|---|---|
| `STANCE` | `hop_stance_thrust` (near idle) | **active** |
| `LAUNCH` | `hop_burst_thrust` for `hop_burst_ms` | **active** |
| `ASCENT` | `hop_idle_thrust` | **active** |
| `APEX` | `hop_idle_thrust` | **active** |
| `DESCENT` | `hop_idle_thrust` | **active** |

The attitude loop **never stops running**, in any phase. This is the single
most important implementation rule in this document. The vehicle is
aerodynamically unstable and there is no passive righting moment; if the
attitude loop pauses for even a fraction of the ballistic phase, the vehicle
tumbles and lands on its side. Rotors at idle still produce differential thrust
and therefore still produce control torque — use it.

Corollary: **the mixer must reserve control authority even at idle collective.**
If idle is zero, the mixer has nothing to differentiate and attitude control
silently dies exactly when it is needed. `hop_idle_thrust` must be high enough
to leave headroom for torque. Start conservative.

### Timing the burst

The burst must be applied while the spring is *extending*, not before and not
after. Applied early it fights the compression; applied late it does nothing
because the foot has already left the ground. Detect bottom-of-stroke from
vertical velocity crossing zero while contact is latched, then fire.

`hop_burst_ms` and `hop_burst_thrust` are the two numbers that determine hop
height. Tune them on a tether, one at a time.

### Regulating hop height

Closed loop, once per hop — not within a hop:

```
error = hop_height_target - measured_apex_height
hop_burst_thrust += Kp_hop · error      (clamped)
```

This is a slow outer loop and it should be. Reacting within a single hop is
neither possible (the energy is already committed at launch) nor useful. A
proportional term with a hard clamp is sufficient; do not add integral windup
to a loop that updates once per second.

Note the sensing limit: VL53L0X is spec'd to 2 m and realistically manages
~1.2 m in daylight, while hops reach ~1.6 m. **Apex height will often be
measured by the inertial filter alone**, coasting through a ranging dropout.
See [estimator.md](estimator.md) — this is exactly why the height filter
carries a vertical velocity and an accel bias state.

## Abort conditions

Leave hop mode immediately, transition to `FLYING`, and level off if:

| Condition | Why |
|---|---|
| tilt exceeds `hop_max_tilt` | about to land on its side, or strike a prop |
| no ground contact for `hop_max_airborne_ms` | leg failed or hopped off a ledge |
| contact detected during `ASCENT` | hit something |
| height estimate invalid and inertial coast expired | flying blind |
| ground slope beyond `hop_max_slope` | leg cannot land safely |
| battery below `hop_min_voltage` | bursts need headroom hover does not |
| link lost | → `FAILSAFE`, not a hop abort |

Aborting to a hover is always safe. Continuing a hop cycle you cannot verify is
not. When in doubt, abort — a hover costs battery, a bad landing costs the
airframe.

### `hop_max_tilt` is set by prop clearance, not by feel

The props mount **below** the arms on this airframe (see
[hardware.md](hardware.md)), which makes them the lowest thing on the vehicle
apart from the leg. Tilt on landing lowers the downhill props toward the
ground, so the tilt limit is not a comfort setting — it is a geometric one:

```
max_landing_tilt  =  arcsin( (leg_length − prop_bottom_offset) / prop_radius_from_CG )
```

Measure the three lengths, compute the angle, and set `hop_max_tilt` **below**
it with margin. Then remember that `ground_slope_rad` eats into the same
budget: landing on a 10° slope with 10° of body tilt can put a prop 20° closer
to the surface than the flat-ground case. The descent pre-tilt exists partly to
manage this, and `hop_max_slope` should be chosen against the same inequality.

A tilt limit picked by feel rather than by this arithmetic produces prop
strikes on hops that otherwise looked completely successful.

## Bring-up order

Do not attempt any of this before hover is proven and trimmed.

1. **Tethered, no leg.** Verify the phase machine transitions correctly using
   forced/simulated contact events. Log everything. No burst.
2. **Leg on, held by hand, motors disabled.** Push the vehicle down onto its
   leg and confirm contact detection and the compression/extension detection
   fire at the right instants.
3. **Tethered, leg on, burst enabled, `hop_burst_thrust` at 25 %.** Expect it
   not to leave the ground. You are checking burst *timing*, not height.
4. Increase burst until it leaves the ground by a few centimetres. Confirm
   attitude holds through the entire airborne phase.
5. Raise `hop_height_target` gradually. Enable closed-loop height regulation
   only once open-loop hops are repeatable.

Every one of these steps is over soft ground or a mat, with a tether, and with
the emergency stop under your thumb.
