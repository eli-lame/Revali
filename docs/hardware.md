# Hardware

## Vehicle

| Function | Part | Bus | Notes |
|---|---|---|---|
| MCU | ESP32-WROOM-32 | — | 240 MHz dual core |
| IMU | SparkFun ICM-20948 | **SPI** | accel + gyro @ 1 kHz, AK09916 mag @ ~100 Hz |
| Height A | VL53L0X | I2C | downward, front-left of CG |
| Height B | VL53L0X | I2C | downward, rear-right of CG |
| Motors | 4× brushless + ESC | LEDC/RMT | quad-X |
| Battery sense | resistor divider | ADC1 | ADC2 is unusable when WiFi/BT is active |

### Put the IMU on SPI, not I2C

The ICM-20948 breakout ships configured for Qwiic/I2C, but the control loop
needs it at 1 kHz. A 12-byte burst read at 400 kHz I2C costs roughly 700 µs of
bus time including addressing and ACKs — about **70 % of a 1 ms tick**, on a bus
also shared with two ranging sensors. That does not fit.

The same read over SPI at 7 MHz costs ~16 µs. Cut the `I2C`/`SPI` select jumper
on the SparkFun board and wire the SPI pads.

If you must stay on I2C for bring-up, run the loop at 500 Hz and put the ToF
pair on the second I2C peripheral. Treat that as temporary.

### Both VL53L0X sensors boot at address 0x29

This is the single most common way this build fails, so handle it first. Every
VL53L0X powers up at `0x29`, and the address is **not** persistent — it resets
on every power cycle. With both on one bus and no mitigation, you get a garbage
mixture of the two.

Wire each sensor's `XSHUT` pin to its own GPIO and sequence them at boot:

1. Drive **both** XSHUT low — both sensors held in reset, bus is clear.
2. Release XSHUT A. Wait ~2 ms for boot.
3. `setAddress(0x30)` on the sensor now answering at `0x29`.
4. Release XSHUT B. Wait ~2 ms. It comes up at `0x29`.
5. Verify: A responds at `0x30`, B at `0x29`, and a bus scan shows exactly two.

Never skip step 5 — a silent failure here reads one sensor twice and the
ground-plane estimate becomes meaningless while looking perfectly plausible.

XSHUT is **not** 5 V tolerant and has an internal pull-up; drive it from a GPIO
directly, and note that some breakouts need the pin driven low rather than
floated to actually hold reset.

### Sensor geometry matters

Record the mounting position of each ToF in the body frame — the estimator
needs the baseline distance between them to turn their difference into a ground
slope, and needs each sensor's offset from the CG to correct for the height
change caused by tilt. Put these in `Parameters`, not in code comments.

Also record the **leg length**: the distance from each ToF to the foot when the
leg is fully extended. That number is the touchdown threshold.

### The VL53L0X measures a cone, not a point

It's a laser (VCSEL) source, but the optics spread it to a **~25° full field of
view** (~±12.5° off boresight), and the receiver's field of view is similarly
wide. The sensor reports the closest/strongest return **anywhere in that
cone**, not a single point straight down. Treat it like a narrow flashlight
beam, not a laser pointer.

The cone's footprint grows with distance:

```
footprint_diameter ≈ 2 × height × tan(12.5°)
```

At 100 mm of height that's already ~44 mm across. Anything within that
footprint — a wall, a bench edge, part of the airframe itself — can be
reported instead of the ground, with no flag that it happened; the reading
just looks like a slightly-wrong but plausible number.

This is a real mounting constraint, not just a bench-test nuisance:

- Each ToF needs a clear ~25° cone below it, out to at least the maximum
  expected hop height, with **nothing else on the airframe** — legs, arms,
  the other ToF's mount — inside that cone.
- During bring-up, test over open floor. A wall or bench edge within roughly
  a footprint-radius of the sensor's boresight, at your test height, will
  contaminate the reading.
- Task `[2.9]`–`[2.11]` in [current_devtasks.md](current_devtasks.md) should
  include physically checking this cone against the frame, not just measuring
  baseline/offset/leg-length in isolation.

### Pin map — vehicle ESP32

This is the pin assignment used throughout this project and by every doc that
references a GPIO number. Wire to it exactly. If you need to deviate (a board
revision, a part you're substituting), update this table first and treat every
other mention of these pins elsewhere in the docs as needing a matching update.

| Signal | GPIO | Wire color (suggested) | Notes |
|---|---|---|---|
| IMU SCLK | 18 | yellow | VSPI default |
| IMU MISO | 19 | green | VSPI default |
| IMU MOSI | 23 | blue | VSPI default |
| IMU CS | 5 | orange | |
| IMU INT | 4 | white | data-ready interrupt — use it, don't poll |
| IMU VCC | 3V3 | red | **not 5 V** |
| IMU GND | GND | black | |
| I2C SDA | 21 | brown | ToF pair, 400 kHz, shared bus |
| I2C SCL | 22 | gray | ToF pair, 400 kHz, shared bus |
| ToF A XSHUT | 25 | violet | front-left sensor |
| ToF B XSHUT | 26 | pink | rear-right sensor |
| ToF VCC (both) | 3V3 | red | |
| ToF GND (both) | GND | black | |
| Motor 1 (front-left) | 32 | — | ESC signal, LEDC or RMT |
| Motor 2 (front-right) | 33 | — | ESC signal |
| Motor 3 (rear-right) | 27 | — | ESC signal |
| Motor 4 (rear-left) | 14 | — | ESC signal |
| Battery sense (divider midpoint) | 34 | red/black twisted pair | ADC1, input-only pin |
| Status LED | 2 | — | onboard, no external wiring |
| Arming buzzer | 13 | — | optional but recommended |

Motor numbering matches the mixer diagram in [control_loop.md](control_loop.md)
— wire ESC signal 1–4 to these GPIOs in that same front-left/front-right/
rear-right/rear-left order, or the mixer's roll/pitch/yaw math will not match
the physical motor it thinks it's driving.

**Avoid** GPIO 6–11 (flash), and be careful with 0, 2, 12, 15 (strapping). A
pull-up on GPIO 12 at boot bricks the boot voltage selection.

### Props mount BELOW the arms (pusher configuration)

The motors hang under the arms with the props on the underside, rather than
sitting on top as on a conventional quad. The physics of flight are unchanged —
every multirotor pushes air down to rise — but three things follow from the
mounting, and all three are easy to get wrong silently.

**1. Prop handedness or motor direction must be flipped.** Inverting a
motor/prop assembly reverses which way the blades push air for the same
electrical rotation direction. Take a motor that produced upward thrust
mounted on top, invert it, and it now pushes air *up* and drives the vehicle
into the ground. Fix it one of two ways, not both:

- fit the opposite-handed prop (swap CW for CCW on each position), or
- reverse the motor by swapping any two of the three ESC phase wires.

Doing both cancels out and leaves you exactly where you started. Pick one
convention, write it down, and verify thrust physically — task `[2.14]`.

**2. The yaw sign in the mixer is likely inverted.** Roll and pitch are
unaffected: they come from differential thrust across the frame geometry, and
front-left is still front-left. But yaw comes from each rotor's *reaction
torque*, which depends on spin direction — so flipping the mounting flips the
relationship between "motor spins this way" and "vehicle yaws that way." Do
not assume the sign convention in [control_loop.md](control_loop.md) is
correct as written; verify it on the bench and flip the yaw terms if needed.

**3. Ground clearance is now a design constraint, not an afterthought.** The
props are the lowest part of the airframe apart from the leg, on a vehicle
whose entire purpose is to repeatedly slam into the ground. The leg must be
long enough that no prop can strike, **including on a tilted landing**:

```
leg_length  >  prop_bottom_offset  +  prop_radius_from_CG × sin(max_landing_tilt)
```

That inequality couples leg length to `hop_max_tilt` in
[hop_mode.md](hop_mode.md) — a longer leg buys more allowable landing tilt,
and a tilt limit set without checking this produces prop strikes on perfectly
"successful" hops. Measure both, do the arithmetic, and record the numbers in
`Parameters`.

**4. Keep the props out of the ToF cone.** The VL53L0X has a wide field of
view (roughly a 25° cone), and props mounted low sit much closer to the plane
the downward sensors look through. A blade clipping the edge of that cone
produces spurious short readings at blade-pass frequency — which the estimator
will happily interpret as the ground rushing up, or as a false ground contact
mid-hop. Mount both ToF sensors well inboard of the prop discs, and verify
with the motors spinning that the ranging output is clean.

### Battery sense wiring

For a 4S pack (14.8 V nominal, ~16.8 V full, ~12.0–13.2 V low cutoff): a
**100 kΩ / 22 kΩ** divider from raw battery+ to GND, tapped at the midpoint
into GPIO 34 (ADC1).

```
 Battery+ (raw) ──[100kΩ]──┬──[22kΩ]── GND
                            │
                            └── GPIO 34 (ADC1_CH6)
```

At full charge: `16.8V × 22/(100+22) ≈ 3.03V` — comfortably under the 3.3 V
ADC ceiling with margin for the nonlinearity noted above.

**Tap raw battery+, before the buck converter — not the buck's regulated 5 V
output.** The buck converter deliberately holds a steady output voltage
regardless of pack voltage, so it hides exactly the sag the low-battery
failsafe needs to see. Two separate wires off battery+: one through the buck
to power the board, one through this divider to the ADC. They must not share a
connection. Use 1% tolerance resistors — divider accuracy sets your voltage
accuracy directly, and this number is what `warn_voltage` / `land_voltage` /
`critical_voltage` in [safety.md](safety.md) are checked against.

Calibrate against a real meter once wired — task `[2.17]` in
[current_devtasks.md](current_devtasks.md).

### ESC and power

- ESCs must see a **valid idle signal before arming**. Boot with outputs at the
  disarmed value and hold there until Safety explicitly arms.
- Give the ESP32 its own regulator. Motor current transients on a shared 5 V
  rail cause brownouts that look exactly like firmware crashes.
- Common ground between ESP32, ESCs, and battery sense is not optional.
- **Remove the propellers** for every bring-up step until the task list says
  otherwise.

## Controller

| Function | Part | Connection |
|---|---|---|
| MCU | ESP32-WROOM-32 | — |
| Power | USB from the laptop | 5 V, via the dev board's onboard regulator |
| Input | SparkFun analog joystick | see below |

### USB power, and what it buys you

The controller is **USB-tethered to the laptop** rather than battery powered.
Two consequences worth designing around:

- **No battery sense on the controller.** It is not going to brown out
  mid-flight, so the low-battery failsafe applies only to the vehicle. The
  controller still needs to handle *the vehicle's* battery telemetry for its
  LED/buzzer, but has no pack of its own to monitor.
- **The USB cable is a free data channel.** It is already physically there for
  power, so it costs nothing to also stream everything the controller knows —
  its own stick state plus the telemetry it receives from the vehicle — up to
  the laptop over USB serial. That is exactly what the ground station in
  [ground_station.md](ground_station.md) is built on, and it is the reason the
  webapp needs no extra radio and no WiFi on the vehicle at all.

A tether is a real constraint on where you can stand while flying — plan the
flight area around cable length, or plan to switch the controller to a battery
and a USB-serial-over-radio bridge later. Note that going battery-powered
would mean rethinking how the ground station gets its data.

### Pin map — controller ESP32 (joystick)

Same rule as the vehicle table above: this is the definitive assignment, not a
suggestion. Applies to controller form 6a (reference joystick) only — the DS4
dongle (form 6b) has no joystick to wire and its own pin map is not yet
written (see [current_devtasks.md](current_devtasks.md) Phase 6).

| Joystick pin | ESP32 GPIO | Wire color (suggested) | Notes |
|---|---|---|---|
| VCC | 3V3 | red | **not 5 V** — ADC inputs are not 5 V tolerant |
| GND | GND | black | |
| XOUT | GPIO 36 (ADC1_CH0) | white | |
| YOUT | GPIO 39 (ADC1_CH3) | yellow | |
| SEL | GPIO 27 | green | active-low, needs `INPUT_PULLUP` |

Use **ADC1 only**. ADC2 is claimed by the radio and returns garbage whenever
the WiFi radio is active — which, on the controller, is always, since ESP-NOW
rides on it.

The SparkFun thumb joystick's `SEL` switch is a plain momentary contact to
ground with no debouncing. Debounce it in firmware (~20 ms) or a single click
will register as three mode changes.

The ESP32 ADC is nonlinear near both rails and does not read a true 0 or 3.3 V.
Calibrate: record the resting centre and both endpoints per axis, store them,
and apply a deadband around centre. Do not assume 2048 is centre — it usually
is not, and an uncalibrated centre becomes a constant tilt command.
