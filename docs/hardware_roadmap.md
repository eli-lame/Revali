# Hardware roadmap

[hardware.md](hardware.md) describes the vehicle as it is wired **today**: an
ESP32-WROOM devkit, a SparkFun ICM-20948 breakout, two VL53L0X breakouts, four
discrete 30 A ESCs, and a buck module, all joined by roughly fifty wires. That
arrangement is correct and it works. It is also most of the vehicle's dry mass
that is not a motor, and on a machine whose entire premise is that launch
energy is scarce, that matters.

This document records how the electronics get from there to a custom board, in
what order, and — more importantly — which things deliberately do **not** get
integrated. It is the design-rationale companion to `hardware.md`: that file
says what to wire, this one says why the board looks the way it does.

---

## The central decision: two boards, not one

The intuitive move is a single custom board carrying everything — MCU, IMU,
buck, battery sense, and the motor power distribution. **Do not do that.** The
vehicle gets two boards:

```
        +------------------------------+
        |  REVALI FC  (custom)         |  MCU, IMU, 5 V buck, 3V3 LDO,
        |  30.5 x 30.5 mounting        |  battery divider, USB, connectors
        +--------------+---------------+
                       |  8-pin JST-SH ribbon
                       |  VBAT . GND . S1-S4 . current sense . telemetry
        +--------------+---------------+
        |  4-IN-1 ESC  (off the shelf) |  four ESCs + power distribution,
        |  30.5 x 30.5, 3-6S, 45-60 A  |  battery leads and 12 phase wires
        +------------------------------+
```

The 4-in-1 ESC **is** the power distribution board. It is not an extra
component sitting alongside a PDB; it is the PDB, with the four speed
controllers already integrated, the bulk capacitor already placed next to the
FETs, and the current shunt already installed.

### Why the motor current does not come onto our board

It is tempting to reason that since VBAT is already present for the buck, the
motor rails may as well come too. The voltage is the same. The current is not:

| Path | Current at 14.8 V |
|---|---|
| Buck feed (5 V @ 1 A out, ~85 % efficient) | **~0.4 A** |
| Four ESCs at peak | **~120 A** |

That is a factor of three hundred, and it changes the problem completely.

**Copper.** At 2 oz, carrying 120 A needs roughly **44 mm of copper width**
even allowing a punishing 45 °C temperature rise — wider than the whole board.
A 60 A launch burst still needs ~17 mm, per path, in four directions. This is
why commercial PDBs are effectively busbars: solid pours with the soldermask
removed so solder can be flooded on to add metal by hand.

**di/dt.** ESCs switch at 24–48 kHz with sharp edges, and each one needs a
low-ESR capacitor within about 5–10 mm of its power pads. Long pours from a
central battery pad out to four corner ESCs add loop inductance, and that
inductance produces voltage spikes that destroy FETs. It is the most common way
a home-made PDB kills bought ESCs — and it kills them in a way that looks like
the ESCs were faulty.

**Ground bounce.** 120 A of pulsed 30 kHz current sharing copper with a 3.3 V
SPI bus and a gyro read at 1 kHz is a noise problem that commercial designs
avoid by putting power and flight control on separate boards.

**Thermal.** 120 A through even 1 mΩ of copper is 14 W. That board becomes a
heater sitting under the IMU, and gyro bias drifts with temperature.

**Blast radius.** Lift a pad soldering 14 AWG battery leads and you have
destroyed the MCU, the IMU and the buck along with it. Separate boards mean a
dead power stage is a thirty-dollar replacement, not a respin and a three-week
wait.

### It is also the lighter option

A custom PDB does not remove the twelve motor phase wires, and it leaves four
ESC bricks on the arms. A 4-in-1 removes the bricks and shortens the phase
wires to a couple of centimetres each.

| | Discrete ESCs + custom PDB | 4-in-1 |
|---|---|---|
| ESC mass | ~7 g × 4 = 28 g | 12–14 g total |
| PDB mass | 8–12 g (2 oz copper) | 0 — it is the PDB |
| Phase wire | full arm length × 12 | ~2 cm × 12 |
| FC ↔ power interface | 16 wires | one ribbon |

Roughly **25–30 g saved**, which is the thing we were trying to buy in the
first place.

### What our board keeps

Everything that made a custom board attractive, minus the copper:

- MCU and IMU as chips/modules rather than breakouts
- 5 V buck **on our board**, fed from VBAT up the ribbon
- Battery divider tapping **raw** VBAT before regulation, as
  [hardware.md](hardware.md) requires — the buck hides the sag the failsafe
  needs to see
- USB, status LED, buzzer, connectors

Do not take 5 V from an ESC BEC even when one is present. `hardware.md` already
states the rule — motor current transients on a shared 5 V rail cause brownouts
that look exactly like firmware crashes — and a BEC on the ESC board sits in
the middle of the switching it would need to be immune to.

### The ESC interface — Skystars KO50A

The 4-in-1 selected for Stage 1. Confirmed specifications:

| | |
|---|---|
| Board / mounting | 41 × 46 mm, 30.5 × 30.5 M3, soft-mounts included |
| Weight | 13.3 g |
| MCU / firmware | BB21 @ 48 MHz, BLHeli_S 16.x, DShot150/300/600 |
| Pack | 3–6 S (silkscreened 25 V — **stay on 4 S**, 6 S at full charge is 25.2 V) |
| Current sensor | Yes |
| **BEC** | **No** — the 5 V rail is ours to generate |
| In the box | ESC, 8-pin cable, bulk capacitor |

The 8-pin JST-SH pinout, confirmed against the hardware:

| Pin | Signal | Goes to |
|---|---|---|
| 1 | GND | board ground |
| 2 | **BAT** | raw pack voltage → buck input and battery divider, nothing else |
| 3 | S1 | motor 1 signal |
| 4 | S2 | motor 2 signal |
| 5 | S3 | motor 3 signal |
| 6 | S4 | motor 4 signal |
| 7 | **NC** | not connected — no separate telemetry line on this board |
| 8 | CURR | analog current-sense output → ADC1 |

Two things follow. **BAT is raw pack voltage**, 16.8 V at full charge on 4 S — it
goes to the buck input and the divider and nowhere else. In particular it must
never reach the devkit's `VIN` pin, whose onboard AMS1117 is rated to roughly
15 V.

And pin 7 being NC means there is **no separate ESC telemetry wire**. That does
not rule out RPM feedback: bidirectional DShot returns telemetry on the signal
line itself, so flashing Bluejay onto the BLHeli_S firmware gets RPM back over
S1–S4 with no extra conductor.

---

## Power topology and the on/off switch

There is exactly **one battery connection in the vehicle**: the XT60 solders to
the 4-in-1's battery pads, alongside the bulk capacitor. Nothing high-current
ever reaches our board.

```
  LiPo ──XT60──► 4-IN-1 battery pads (B+ / B-)
                    |
                    +--> internally: the four ESCs
                    |
                    +--> 8-pin ribbon: BAT, GND --> REVALI FC
                                                      |
                                                      +--> [FC power switch]
                                                             |
                                                             +--> buck --> 5 V --> devkit VIN
                                                             +--> 100k/22k divider --> ADC1
```

### There is no switch in the motor path, and there will not be one

The v1 prototype schematic placed a screw terminal in series with battery
positive, annotated "switch goes here." That is deleted, for two reasons.

**A switch there carries full motor current** — roughly 120 A peak. A switch
genuinely rated for that is a contactor: heavier than everything else on the
board put together, and it inserts contact resistance into the highest-current
path in the vehicle.

**Moving it to the FC does not achieve the same thing.** A switch that only
cuts our board's BAT feed leaves the ESCs fully powered from the pack. This is
not especially *dangerous* — DShot frames are checksummed and BLHeli will not
arm without a valid throttle-low signal, so floating inputs on an unpowered FC
do not spin motors — but it disconnects nothing that matters, while looking
like it does.

**The battery connector is the disconnect.** No FPV multirotor has a power
switch. Unplugging the XT60 is the only guaranteed-safe state, and an
anti-spark XT60 handles the inrush spark from the ESC's bulk capacitor.

### Power states

| State | How to get it | Motors |
|---|---|---|
| Fully off | XT60 unplugged | dead |
| FC only — bench work, flashing | USB connected, **no pack** | physically dead |
| Full system, safe | XT60 plugged in, firmware disarmed | live bus, held at disarmed value |
| Full system, armed | Safety arms — see [safety.md](safety.md) | live |

The second row is the important one: the devkit's USB gives a better
"controller running, motors physically dead" mode than any switch, for free.
That is the normal bring-up configuration.

### If a switch is fitted anyway

A small switch in the FC's BAT feed — downstream of the ribbon entry, upstream
of both the buck and the divider, so the whole board goes dead with no standby
drain — is a legitimate bring-up convenience: it power-cycles the FC for
reflashing without unplugging the pack. That path carries ~0.4 A at 16.8 V, so
any slide switch or even a removable 2-pin jumper is sufficient.

Label it in the schematic as **`FC POWER — NOT A SAFETY DISCONNECT`**. It is
not one, and in three months that label is the only thing that will say so.

---

## The ToF sensors stay off the board

This is a real constraint, not a convenience. [hardware.md](hardware.md)
requires each VL53L0X to sit at a recorded position in the body frame, with a
measured baseline between them, a known offset from the CG, and a clear ~25°
cone below it containing no part of the airframe. Front-left and rear-right of
the CG, spaced as widely as the frame allows — the baseline is what turns their
difference into a ground slope, and a short baseline makes the estimate noisy.

A sensor soldered to a 36 mm board is at the CG with a baseline of a few
centimetres. The FC gets a **4-pin JST-SH connector per sensor**; the sensors
mount where the geometry needs them.

The IMU is the opposite case: it belongs at the CG, on the board, soft-mounted.

---

## Staging

Two goals are in tension here: learning PCB design, and finishing the vehicle.
They conflict because a board revision is slow — about two weeks to design, two
to fabricate and ship, and first boards usually need one respin. That is
roughly seven weeks before working hardware exists. **If the firmware is
blocked behind the board, that cost lands on the whole project.**

So the tracks are kept independent. Each stage below flies on its own.

### Stage 0 — current: breadboard (done)

WROOM devkit, ICM-20948, two VL53L0X, discrete ESCs. Ugly, working. Firmware
development proceeds here and is never blocked by the board.

### Stage 1 — 4-in-1 ESC swap

Replace the four discrete ESCs and their harness with one 4-in-1. No PCB work.
This alone removes most of the wiring mass and validates the ribbon interface
the custom board will mate to.

### Stage 2 — carrier board

A 30.5 × 30.5, **2-layer, hand-solderable** board that carries the existing
devkit in a 2.54 mm socket, the breakout modules, the buck, the battery
divider, and the connectors. No bare chips, no fine pitch, nothing that cannot
be reworked with an iron.

This stage is easy to skip and should not be. It is cheap and quick, it teaches
the entire pipeline end to end — schematic, footprints, layout, DRC, Gerbers,
ordering, assembly, bring-up — with almost nothing at stake, and every
schematic block and footprint it produces is reused by Stage 3. It also solves
the wiring problem immediately: roughly fifty wires become about six.

### Stage 3 — integrated FC

4-layer, assembled by the fab, bare module + IMU chip + integrated buck +
USB-C. Designed as a **drop-in replacement** for Stage 2: same pin functions,
same connector positions, same firmware binary.

Built this way, Stage 3 is optional. If it works, it goes in the vehicle. If it
does not, the vehicle still flies on Stage 2 hardware, and the project is not
hostage to a PCB revision.

---

## Link: ESP-NOW now, ELRS later

The eventual target is the standard split used by serious builds:

- **Control in** — ExpressLRS receiver over CRSF on a UART. Sub-5 ms, real link
  statistics, proper failsafe, telemetry on the return path.
- **Telemetry out** — BLE or USB to the ground station.

We are **not** switching yet. ESP-NOW works today, the vehicle's link layer is
already built around it, and Stages 1–3 already change the power architecture,
the board, and the mechanical layout. Changing the radio at the same time means
that when something misbehaves there is no way to tell which change caused it.

Note that BLE is not a candidate for the *control* link regardless — its
minimum connection interval is 7.5 ms with real jitter, and it is not designed
for deterministic control. BLE is for telemetry and configuration only, which
is exactly how commercial flight controllers use it.

**The one thing to do now:** put a 4-pin JST-SH footprint (5 V, GND, TX, RX) on
a spare UART on both the Stage 2 and Stage 3 boards, wired and unpopulated.
Four pads and about thirty cents. With them, adopting ELRS later is a firmware
change. Without them, it is a board respin.

When the switch does happen, two consequences follow. The `controller/` project
leaves the critical path — CRSF carries the same intent the wire protocol in
[communication.md](communication.md) already defines, so the vehicle stays
controller-agnostic and the custom transmitter becomes an optional input rather
than a dependency. And [ground_station.md](ground_station.md) loses its current
transport: the dashboard is fed over the controller's USB cable today, and
would instead talk to the vehicle directly.

---

## Part selection

| Block | Part | Why |
|---|---|---|
| 4-in-1 ESC | **Skystars KO50A** — 30.5 × 30.5, 3–6 S, 50 A | Ordered. Full interface above |
| 5 V regulator | **Pololu D24V10F5** — 5.1–36 V in, 5 V @ 1 A, 12.7 × 17.8 mm | Ordered. Fixed output, solder flat, 33 µF+ electrolytic at VIN per Pololu's lead-length warning |
| MCU | ESP32-WROOM-32E (bare module) | Same silicon as the devkit — zero firmware port. Castellated and hand-solderable |
| MCU (upgrade) | ESP32-S3-WROOM-1 | Native USB and BLE 5. Only worth it once BLE telemetry is wanted; verify the footprint difference before assuming drop-in |
| IMU | **ICM-42688-P** | Much better anti-alias filtering than the ICM-20948, which matters a great deal on a vehicle that repeatedly slams into the ground. Drop the magnetometer — it is useless indoors beside 120 A of motor current |
| ToF | **VL53L1X** | Programmable region of interest, which directly attacks the wide-cone problem documented in [hardware.md](hardware.md). VL53L5CX (8 × 8 zones) if off-axis rejection needs to be done properly |
| 5 V rail (Stage 3) | Discrete synchronous buck IC rated **≥ 36 V input** | Replaces the module once switching layout is worth learning on its own |
| 3.3 V rail | Stage 2: the devkit's onboard AMS1117 (~50 mA of sensors — ample). Stage 3: an LDO from 5 V, separate feed for the IMU | Low noise for the gyro |
| Protection | TVS on VBAT + low-ESR bulk at the pads | Non-negotiable beside brushless motors |

**On ESC voltage rating:** a "6–18 V" (2–4 S) part is not acceptable here. A 4 S
pack is 16.8 V at full charge, leaving 1.2 V of margin, and ESCs generate
regenerative spikes when throttle drops sharply. On a hopper that is not an
edge case — it is every single cycle: full-send launch, immediate cut,
ballistic phase, catch on landing. Specify 3–6 S (≈26 V, 35 V capacitors).

Choose every part against the fab's assembly library **before** finalising the
schematic. On a board this size, hand-soldering an LGA IMU is miserable; part
availability will constrain the BOM more than preference does.

---

## Rules specific to a hopping vehicle

This airframe's purpose is repeated hard impact, which changes the board rules:

- No tall electrolytics standing unsupported. Solid polymer capacitors, and
  stake anything with mass.
- Soft-mount the board on gummies or TPU standoffs. A rigidly-mounted gyro on
  this vehicle sees impact spikes that alias into the attitude estimate.
- Prefer solder pads and staked wires anywhere a connector could walk out of
  its housing under shock.
- Conformal coat after bring-up.
- Buy spares. Two ESCs, a spare MCU, and many propellers — this machine is
  designed to hit the ground.

---

## Defects in the v1 prototype schematic

Recorded here because they are cheap to fix now and expensive to find during
bring-up. That board has not been fabricated and is superseded by the staging
above, but the same nets carry forward.

**ESC signal numbering is reversed relative to the pin map.** The schematic
assigns `ESC1_SIG → GPIO14, ESC2 → GPIO27, ESC3 → GPIO33, ESC4 → GPIO32`. The
pin map in [hardware.md](hardware.md) assigns Motor 1 (front-left) to GPIO 32,
Motor 2 to 33, Motor 3 to 27 and Motor 4 to 14 — so schematic `ESC1` is the pin
map's Motor 4, and the ordering is inverted end to end. The mixer would drive
the wrong physical motor for every roll and pitch term. Combined with the
unresolved yaw-sign question from the pusher-prop mounting, this would be very
difficult to diagnose in the air. One of the two must change;
[hardware.md](hardware.md) is the source of truth.

**Battery sense is absent.** GPIO 34, 35, 36 and 39 are all marked no-connect.
The 100 kΩ / 22 kΩ divider specified in `hardware.md` is not on the board, so
`warn_voltage`, `land_voltage` and `critical_voltage` in [safety.md](safety.md)
have no input and the low-battery failsafe cannot function. Two resistors and a
capacitor on a board that already carries VBAT.

**No bulk capacitance, TVS, or reverse-polarity protection on VBAT.**
Acceptable while every power component is a module with its own input caps; it
becomes ours the moment the buck is on our board.

---

## Open decisions

- **Frame geometry.** Motor-to-motor diagonal, and confirmation that the leg's
  travel clears a centre-mounted stack. The leg moves below the frame and does
  not pass through it, so a centred stack is expected to be fine — this needs
  checking against the actual frame before layout.
- **MCU for Stage 3** — WROOM-32E (no port, no BLE) or S3 (port, BLE).
  Deferred until the BLE telemetry path is actually wanted.
- **Current sensing.** Deferred, not dropped. The KO50A exposes `CURR` on pin 8,
  and it would give joules-per-hop directly — the number that validates the
  efficiency premise this whole vehicle is built on. Stage 2 routes the trace
  and leaves the resistor unpopulated, so enabling it later costs one 0805 part
  plus a calibration. Two things are unresolved until then: the full-scale
  output voltage, which Skystars does not publish and which must be measured
  before anything is connected to a GPIO, and the volts-per-amp scale factor,
  which needs a meter.

Settled since this document was written: the KiCad projects now live under
`hardware/`, so the board and the firmware version together. v1 is archived
there as `hardware/v1-prototype/` — see
[hardware/README.md](../hardware/README.md) for the repository layout and the
library conventions.
