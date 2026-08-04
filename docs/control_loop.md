# Control loop

## Tick order

One fixed-rate 1 kHz tick. Order is not arbitrary — each stage consumes the
previous stage's output from *this* tick, so a wrong order silently inserts a
tick of latency into the attitude loop.

```
  1  scheduler wakes                       (t = k · 1 ms)
  2  read IMU                              (SPI, data-ready driven)
  3  estimator.update(now_us)              attitude + height + contact
  4  poll link → newest command            non-blocking
  5  mode manager → DesiredState           joystick mapping, state machine
  6  hop sequencer → phase, thrust override (if HOPPING)
  7  controller → ControlOutput            cascaded PID
  8  mixer → MotorOutput                   quad-X allocation
  9  safety → veto / clamp / kill          LAST WORD
 10  motor manager → ESCs                  write
 11  sub-rate tasks (see scheduler.md)
 12  wait for next tick
```

Steps 2–10 are the hard-real-time path. Nothing in them may block, allocate, or
print. Sub-rate work happens at step 11 in whatever time is left.

## Cascaded PID

Two nested loops. The inner one is the one that keeps you alive.

```
DesiredState.roll/pitch ──► [ANGLE P] ──► rate setpoint ──┐
                                                          ▼
VehicleState.angular_velocity ──────────► [RATE PID] ──► torque
                                                          │
DesiredState.yaw_rate ────────────────────────────────────┤
                                                          ▼
DesiredState.height / climb ──► [ALT PID] ──► collective thrust
                                                          │
                                                          ▼
                                                   ControlOutput
```

| Loop | Input | Output | Type |
|---|---|---|---|
| Angle | angle error | rate setpoint | **P only** |
| Rate | rate error | normalized torque | PID (D on measurement) |
| Altitude | height / climb-rate error | collective thrust | PID |
| Yaw | yaw rate error | yaw torque | PI |

**The angle loop is P-only.** An integral term there fights the rate loop's
integrator and produces a slow oscillation that is genuinely hard to diagnose.
The rate loop's I term handles steady-state error.

**Derivative on measurement, not on error.** A step change in setpoint — which
is exactly what a joystick flick produces — makes the error derivative spike
and kicks the motors. Differentiating the measurement removes that entirely.

**Filter D.** Gyro noise is broadband and the D term amplifies it directly into
motor commands, which heats ESCs and excites frame resonance. A low-pass at
60–90 Hz is a reasonable start.

**Clamp the integrators, and stop integrating when saturated.** If the mixer is
already asking for more than the motors can give, integrating further error
does nothing but build windup that has to unwind later — as an overshoot, at
the worst possible time. Freeze the I term whenever the mixer reports
saturation.

### Hopcopter-specific: reset on ground contact

During `STANCE` the vehicle is physically held by the leg. The attitude error
is not something the rotors can correct, but the integrator will happily wind
up trying. **Freeze or bleed the rate integrators while ground contact is
latched**, and reset them at launch. Skipping this produces a violent attitude
kick on every takeoff, and it will look like a tuning problem rather than a
bookkeeping one.

## Mixer — quad X

```
        M1 (CW)      M2 (CCW)          front
             \        /                  ▲
              \      /                   │
               ┌────┐
               │ CG │
               └────┘
              /      \
             /        \
        M4 (CCW)     M3 (CW)
```

```
M1 = thrust  - roll  + pitch  + yaw
M2 = thrust  + roll  + pitch  - yaw
M3 = thrust  + roll  - pitch  + yaw
M4 = thrust  - roll  - pitch  - yaw
```

Verify the signs on the bench with props off, one axis at a time, by tilting
the frame by hand and confirming the correct motors speed up to oppose the
motion. A sign error here is instantly unflyable and instantly obvious — if you
check for it. Check for it.

**The yaw signs above are not trustworthy as written on this airframe.** The
props mount *below* the arms (pusher configuration — see
[hardware.md](hardware.md)), which inverts the relationship between a motor's
spin direction and the yaw torque it produces. Roll and pitch are unaffected,
because they come from differential thrust across the frame geometry and the
geometry has not changed. Yaw comes from reaction torque, and that has.

Treat the `yaw` column as a hypothesis to be confirmed on the bench, not a
given. Verify it independently of roll and pitch, and flip all four yaw terms
together if the vehicle yaws the wrong way.

### Saturation

When any motor exceeds `[idle, max]`, do **not** just clip it. Clipping
silently changes the torque the vehicle actually produces, in a direction you
did not choose, precisely when authority matters most.

Prioritize in this order:

1. **Roll/pitch torque** — losing this means losing the vehicle.
2. **Collective thrust** — sag a little, that is survivable.
3. **Yaw torque** — sacrifice first, nobody notices.

Implementation: compute the desired mix, find the excess, and reduce collective
thrust until the roll/pitch components fit. Report saturation to the controller
so it can freeze its integrators.

This matters enormously during hops. At `hop_idle_thrust` the collective is
already low, so there is little room *below* to sacrifice — which is why
`hop_idle_thrust` must be set high enough to leave torque headroom. See
[hop_mode.md](hop_mode.md).

## Motor output

- Normalized `0.0–1.0` from the mixer, mapped to the ESC protocol at the last
  moment. Nothing upstream of the motor manager knows about pulse widths.
- **Thrust is not linear in throttle** — it is roughly quadratic. Applying a
  linearization curve means your PID gains stay valid across the throttle
  range instead of being correct only near hover. This matters more on a
  hopcopter than a normal quad, because a hop deliberately sweeps the full
  throttle range on every cycle.
- Write all four channels in one operation each tick. Staggered writes put a
  varying torque impulse into the frame.
- On disarm, go to the disarmed value. On emergency stop, go to zero.

## Testing without flying

Everything above compiles on a host with no ESP32. Build a native PlatformIO
environment and test:

- **Mixer**: known torque in → expected per-motor values out, including
  saturation cases.
- **PID**: step response against a simple rigid-body model; check for
  overshoot, windup, and D-kick on setpoint steps.
- **Mode manager**: every state transition, especially the illegal ones.
- **Hop sequencer**: scripted contact/velocity sequences → expected phases.
- **Link parser**: truncated frames, bad CRC, replayed and reordered sequence
  numbers, garbage bytes mid-frame.

These are the cheapest bugs you will ever fix. Every one caught here is one not
found at 1.6 m over concrete.
