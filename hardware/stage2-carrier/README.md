# Stage 2 — carrier board

The first custom PCB. Its job is to replace roughly fifty wires with a board,
using only parts that already work, so that the thing being learned is the PCB
pipeline and nothing else. No bare chips, no fine pitch, nothing that cannot be
reworked with an iron.

Context and rationale: [hardware_roadmap.md](../../docs/hardware_roadmap.md).
Pin map source of truth: [hardware.md](../../docs/hardware.md). Build tasks:
`[H.5]`–`[H.10]` in [current_devtasks.md](../../docs/current_devtasks.md).

**Deliberately not on this board:** motor current, the ToF sensors themselves
(they mount out at the frame — the estimator needs the baseline), and ELRS
(reserved footprint only). A discrete switching regulator waits for Stage 3.

---

## Board

| | |
|---|---|
| Layers | 2, 1 oz copper, 1.6 mm FR4 |
| Outline | **44 × 56 mm** (see the note below) |
| Mounting | 4 × Ø3.2 mm on a 30.5 × 30.5 mm square, centred |
| Assembly | Hand-soldered, all parts on the top side |

### Why the board is this big

The 30-pin ESP32 devkit is about **52 × 25 mm** — longer than the 36 × 36 mm a
standard 30.5 flight controller occupies. It does not fit on a conventional FC
outline, so the board grows and overhangs the stack fore and aft.

That is an acceptable trade at this stage and arguably a benefit: a roomy
2-layer board is dramatically easier to route, and every component stays
reachable for rework. Weight optimisation is Stage 3's job, and Stage 3 gets it
by dropping the devkit for a bare module.

**Check the outline against the actual frame before ordering.** If 56 mm does
not clear, the options are to rotate the devkit, or to mount the regulator and
passives *underneath* it — a socketed devkit sits ~10 mm above the board, which
is plenty of room, at the cost of having to pull the devkit for rework.

### The footprint check that catches the classic first-board failure

Devkit header spacing is **not consistent between clones** — 0.9″ and 1.0″ row
spacing both exist under the same "30-pin ESP32 devkit" name. A library
footprint that does not match the board in your hand produces a PCB that is
scrap on arrival.

Before ordering: **print the footprint 1:1 on paper and lay the physical devkit
on top.** Check row spacing, pin count, and overall length. Do the same for the
Pololu module and both JST connectors. This takes five minutes and is the
single highest-value check in the whole process.

---

## Connectors

| Ref | Type | Purpose |
|---|---|---|
| `J1` | JST-SH 1.0 mm, 8-pin | ESC ribbon to the KO50A |
| `J2` | 2 × 1×15 female header, 2.54 mm | ESP32 devkit socket |
| `J3` | 1×7 header, 2.54 mm | IMU (ICM-20948 breakout) |
| `J4` | JST-SH 1.0 mm, 5-pin | ToF A (front-left) |
| `J5` | JST-SH 1.0 mm, 5-pin | ToF B (rear-right) |
| `J6` | JST-SH 1.0 mm, 4-pin | **ELRS / CRSF — reserved, unpopulated** |
| `J7` | 1×2 header, 2.54 mm | Buzzer |
| `J8` | 1×2 header, 2.54 mm | FC power switch — optional, see below |

### J1 — ESC ribbon (KO50A)

| Pin | Signal | Net | ESP32 |
|---|---|---|---|
| 1 | GND | `GND` | — |
| 2 | BAT | `BAT` | — (buck + divider only) |
| 3 | S1 | `ESC1` | GPIO 32 |
| 4 | S2 | `ESC2` | GPIO 33 |
| 5 | S3 | `ESC3` | GPIO 27 |
| 6 | S4 | `ESC4` | GPIO 14 |
| 7 | NC | — | leave unconnected |
| 8 | CURR | `CURR` | GPIO 35, through the filter below |

GPIO 32/33/27/14 are Motor 1/2/3/4 = front-left / front-right / rear-right /
rear-left, matching the mixer. **Which physical motor the ESC's own output 1
drives is decided when you solder the motors to the ESC**, not by this board —
so the mapping still has to be verified empirically in `[H.3]`. The v1
schematic got this backwards; do not assume.

---

## Power

```
J1.2 BAT ──┬── D1 (TVS) ──┬── C1 ──┬── [J8 switch] ──┬── U1 VIN ── U1 VOUT ── +5V
           │              │        │                 │
          GND            GND      GND                └── R1 ── divider ── GPIO 34

  +5V ──┬── devkit VIN ── (devkit AMS1117) ── +3V3 ── IMU, ToF ×2
        ├── J6.1 (ELRS, reserved)
        ├── buzzer circuit
        └── D2 power LED
```

| Ref | Part | Notes |
|---|---|---|
| `U1` | Pololu D24V10F5 | 5.1–36 V in, 5 V @ 1 A. Solder **flat** through the 3 pads, not into headers — a socketed module works loose under impact. Stake with epoxy |
| `D1` | SMBJ20A TVS | 20 V standoff (clear of 16.8 V full charge), ~32 V clamp — under the Pololu's 36 V limit. **4 S only**; 6 S would sit above the standoff voltage |
| `C1` | 100 µF 35 V low-ESR electrolytic | Pololu's own docs warn that leads longer than a few inches create an LC spike at power-up that can exceed the module's rating. The ribbon plus trace run qualifies. Add `C2` 100 nF ceramic beside it |
| `C3` | 10 µF + 100 nF on `+5V` | |
| `C4` | 10 µF + 100 nF on `+3V3` | Plus 100 nF at each sensor connector |

Nothing else connects to `BAT`. It is 16.8 V at full charge and **must never
reach the devkit's `VIN` pin**, whose AMS1117 is rated to roughly 15 V.

### Battery divider — GPIO 34

```
BAT ──[ R1 100 kΩ 1% ]──┬──[ R2 22 kΩ 1% ]── GND
                        ├──[ C5 100 nF ]── GND
                        └── GPIO 34 (ADC1_CH6, input-only)
```

16.8 V × 22/122 ≈ **3.03 V** at full charge, under the 3.3 V ceiling with room
for the ADC's nonlinearity near the rail. Quiescent draw ~138 µA. Use 1 %
resistors — divider accuracy sets voltage accuracy directly, and this is the
number `warn_voltage` / `land_voltage` / `critical_voltage` in
[safety.md](../../docs/safety.md) are checked against. Calibrate against a
meter — task `[2.17]` / `[H.10]`.

### Current sense — GPIO 35, routed but deferred

```
J1.8 CURR ──[ R3 DNP ]──┬──[ C6 100 nF ]── GND
                        ├──[ R4 DNP ]── GND     (divider leg, if needed)
                        └── GPIO 35 (ADC1_CH7, input-only)
```

**Current sensing is deliberately not being used on this board.** The trace and
the footprints exist; `R3` is not populated, so the net is open and nothing
reaches GPIO 35. There is nothing to measure, verify or calibrate at bring-up —
skip it entirely.

The reasoning matches the reserved ELRS footprint: one trace and two pads on a
board being fabricated anyway, versus a respin if it turns out to be wanted.
Voltage sensing — which the failsafe genuinely requires — is populated and
working regardless.

**If it is enabled later**, two things have to happen first. Skystars does not
publish the KO50A's full-scale output voltage, and anything above 3.3 V damages
the ESP32 — so with props off, run the motors up and meter `J1.8`. If full
scale is at or under 3.3 V, fit `R3` as 1 kΩ and leave `R4` DNP; if it exceeds
3.3 V, size `R3`/`R4` as a divider. Then calibrate volts-per-amp against a
clamp meter. Until that happens, leave both unpopulated.

### J8 — FC power switch (optional)

A 1×2 header in the `BAT` net, downstream of `D1`/`C1` and upstream of both the
regulator and the divider, so the whole board goes dead with no standby drain.
Carries ~0.4 A at 16.8 V; any slide switch or a removable jumper suffices.

Fit a jumper shunt here if you do not want a switch — the net must be closed
for the board to power up.

**Silkscreen it `FC PWR — NOT A SAFETY DISCONNECT`.** It isn't one: the pack is
still connected and the ESC bus is still live. See the power-states table in
[hardware_roadmap.md](../../docs/hardware_roadmap.md).

---

## Sensors

### J3 — IMU, SPI

| Pin | Signal | ESP32 |
|---|---|---|
| 1 | 3V3 | — |
| 2 | GND | — |
| 3 | SCLK | GPIO 18 |
| 4 | MISO | GPIO 19 |
| 5 | MOSI | GPIO 23 |
| 6 | CS | GPIO 5 |
| 7 | INT | GPIO 4 |

Place `J3` **at the mounting-hole centroid** — the IMU belongs at the CG. The
breakout solders directly to this header; soft-mount the whole board rather
than the sensor. The SparkFun board's I2C/SPI jumper must be cut, per
[hardware.md](../../docs/hardware.md).

### J4 / J5 — ToF pair, I2C

| Pin | Signal | ESP32 | J4 (ToF A) | J5 (ToF B) |
|---|---|---|---|---|
| 1 | 3V3 | — | | |
| 2 | GND | — | | |
| 3 | SDA | GPIO 21 | shared | shared |
| 4 | SCL | GPIO 22 | shared | shared |
| 5 | XSHUT | | GPIO 25 | GPIO 26 |

Separate XSHUT lines per sensor are what make the boot address-assignment
sequence possible — both sensors come up at `0x29` and the address does not
persist. See [hardware.md](../../docs/hardware.md).

**Provide I2C pull-up footprints (`R5`, `R6`, 4.7 kΩ to 3V3) but leave them
DNP.** The VL53L0X breakouts carry their own pull-ups; two breakouts already
put two sets in parallel, and adding a third would over-drive the bus. The
footprints exist for the day the sensors are bare chips.

XSHUT is not 5 V tolerant and has an internal pull-up. Drive it directly from
the GPIO.

---

## Remaining circuits

### Buzzer — GPIO 13

An active 5 V buzzer draws more than a GPIO can source, so switch it:

```
GPIO 13 ──[ R7 100 Ω ]── gate, Q1 (2N7002)
                          ├── R8 10 kΩ gate→GND   (holds it off during boot)
         +5V ── buzzer ── drain
                  └── D3 1N4148 flyback, if the buzzer is magnetic
```

`R8` matters: without it the gate floats while the ESP32 boots and the buzzer
can chirp or latch on at power-up.

### Status LED — GPIO 2

Onboard on the devkit. No external part. Do not load GPIO 2 with anything else
— it is a strapping pin.

### Power LED

`+5V ──[ R9 1 kΩ ]── D2 ── GND`. Not required, genuinely useful: it tells you
at a glance whether the regulator is alive, which is the first question during
bring-up.

---

## Protection and defensive design

Cheap things that make a first board survivable. None of these cost meaningful
space or money; all of them cost a respin if left out.

### The devkit can be inserted backwards — prevent it on the silkscreen

Two 1×15 sockets are mechanically symmetric. A devkit rotated 180° puts `+5V`
where `GND` should be and will destroy it, the regulator, or both, instantly.
Nothing electrical stops this.

Silkscreen a **full devkit outline** with the USB end clearly marked —
`USB THIS END` in text, plus a filled arrow. Mark pin 1 on both socket rows.
This is the single most likely way to kill this board.

### `F1` — PPTC on the BAT feed

A 0.5 A hold polyfuse in series with `BAT`, right after `J1`. If the regulator
ever fails short, this is what stops the pack from dumping into the board.
Roughly 0.5 Ω, so ~0.2 V and 80 mW at the 0.4 A this path carries — irrelevant
against the Pololu's 5.1 V minimum input.

### `R10`–`R13` — 100 Ω series on the ESC signal lines

One in series with each of S1–S4, at the connector. They limit fault current
into a GPIO if something goes wrong off-board, and damp edges on the DShot
lines. At DShot600 the RC delay against the ESC's input capacitance is a couple
of nanoseconds — immaterial.

### There is no protection against a reversed ribbon

Worth stating plainly, because the instinct is to add a series diode. It does
not help: an 8-pin connector reversed end-for-end swaps pin 1 with pin 8, so
`GND` lands on `CURR` and `BAT` lands on `S4` — putting 16.8 V directly on
GPIO 14. No power-path diode saves that.

The defence is procedural, so do it properly:

- JST-SH is keyed, so the cable cannot go in upside down — but **the connector
  can be soldered rotated**, and the ESC's own cable can be reversed end for
  end.
- Silkscreen pin 1 on `J1` at both the board and, with a paint pen, on the
  cable.
- **Before the first plug-in, meter the assembled cable**: continuity from the
  ESC's `GND` pad to what you believe is `J1` pin 1. Thirty seconds.

### Test points

Exposed 1 mm pads, no header, for: `BAT`, `+5V`, `+3V3`, `GND` (three of them,
spread across the board), `SDA`, `SCL`, and the `GPIO 34` divider node.

These cost nothing and are the difference between clipping a scope probe on in
two seconds and trying to find bare copper on a soldermasked board while a
motor is spinning. Put a ground test point near each signal one.

### `J9` — spare GPIO breakout

A 1×4 header exposing **GPIO 36, GPIO 39** (both input-only, ADC1 — ready for
a future analog sensor or a barometer) and **GPIO 15**, plus a ground. Free
now; a respin later. Mark GPIO 15 on the silkscreen as a strapping pin.

### Mechanical

- **Match the mounting holes to the soft-mount grommets the KO50A ships with.**
  Plain M3 clearance is Ø3.2 mm, but FC grommets commonly need Ø4 mm. Measure
  the supplied parts before committing — the wrong hole means no soft-mounting,
  on a vehicle where gyro isolation genuinely matters.
- **Leave the mounting holes non-plated with a keepout ring**, so a metal
  standoff cannot short into a ground pour.
- **Two Ø2 mm holes near `J4`/`J5`** to zip-tie the ToF cables. Cable flex at
  the connector is a real failure mode on a vehicle that lands hard.
- **Round the board corners**, 1–2 mm radius. Corners are where FR4 chips on
  impact, and square ones chafe wiring.

### Silkscreen

Beyond the above: label every connector pin with both its signal name **and its
GPIO number**, mark polarity on `C1` and `D2`, and put `REVALI STAGE 2 · rev A ·
<date>` on the bottom copper. In November the board will have to explain itself.

---

## Pin budget

Every ESP32 pin this board uses, checked against the constraints in
[hardware.md](../../docs/hardware.md):

| GPIO | Use | | GPIO | Use |
|---|---|---|---|---|
| 4 | IMU INT | | 22 | I2C SCL |
| 5 | IMU CS | | 23 | IMU MOSI |
| 13 | Buzzer | | 25 | ToF A XSHUT |
| 14 | ESC 4 | | 26 | ToF B XSHUT |
| 16 | ELRS RX (reserved) | | 27 | ESC 3 |
| 17 | ELRS TX (reserved) | | 32 | ESC 1 |
| 18 | IMU SCLK | | 33 | ESC 2 |
| 19 | IMU MISO | | 34 | Battery sense (ADC1, in-only) |
| 21 | I2C SDA | | 35 | Current sense (ADC1, in-only) |

Clear: 6–11 (flash), 1/3 (UART0 — the console and the bootloader), 12 and 15
(strapping) are all unused. GPIO 2 is the onboard LED only. Both analog inputs
are on **ADC1**, which is required because ADC2 is unusable whenever the WiFi
radio is active — and with ESP-NOW it always is. GPIO 36 and 39 remain free.

### J6 — ELRS, reserved

| Pin | Signal | ESP32 |
|---|---|---|
| 1 | +5V | — |
| 2 | GND | — |
| 3 | TX (to RX on the receiver) | GPIO 17 |
| 4 | RX (from TX on the receiver) | GPIO 16 |

Routed and unpopulated. Stage 2 flies on ESP-NOW; this exists so that adopting
ELRS later is a firmware change rather than a board respin. Note the crossover
— the receiver's TX goes to the ESP32's RX.

---

## Bring-up order

Do not skip steps. Each one makes the next failure cheap.

1. **Bare board, no parts.** Continuity check `BAT`–`GND` and `+5V`–`GND` for
   shorts. Verify `J1` pin 1 really is the pin you think it is.
2. **Power section only** — `U1`, `D1`, `C1`–`C3`, `J8` jumper. Feed `BAT`
   from a **current-limited bench supply at 12 V, limit 100 mA**, not a pack.
   Confirm 5 V out and no heating. Walk the supply up to 16.8 V.
3. **Measure `+5V` at the devkit socket's VIN pin** before the devkit goes in.
   This is the step that saves the devkit.
4. **Devkit in.** Confirm 3V3, blink an LED, serial at 115200.
5. **Sensors.** I2C scan, then the ToF address sequence, then the IMU. `[1.4]`,
   `[2.8]`.
6. **Battery divider.** Calibrate against a meter. (Nothing to do for `CURR` —
   it is unpopulated.)
7. **ESC ribbon, props off.** Identify which physical motor each of S1–S4
   drives and record it. `[H.3]`.

### Before the devkit is ever powered from the board

Check whether your devkit's `VIN` pin is tied straight to the USB 5 V rail —
many are, with no isolation diode. Continuity between the USB connector's 5 V
pin and the `VIN` header pin, board unpowered. If they are tied, decide
deliberately whether to flash with the pack connected; if there is a diode,
there is nothing to think about. Either way: **props off.**

---

## Bill of materials

| Ref | Part | Qty |
|---|---|---|
| `U1` | Pololu D24V10F5 | 1 |
| `D1` | SMBJ20A TVS, SMB | 1 |
| `D2` | LED, 0805 | 1 |
| `D3` | 1N4148 (magnetic buzzer only) | 1 |
| `Q1` | 2N7002 N-MOSFET, SOT-23 | 1 |
| `C1` | 100 µF 35 V low-ESR electrolytic | 1 |
| `C2`–`C6` | 100 nF 0805 | 5 |
| — | 10 µF 0805 (on +5V and +3V3) | 2 |
| `R1` | 100 kΩ 1 % 0805 | 1 |
| `R2` | 22 kΩ 1 % 0805 | 1 |
| `R3`, `R4` | current sense — **both DNP**, deferred | 2 |
| `R5`, `R6` | 4.7 kΩ I2C pull-ups — **DNP** | 2 |
| `R7` | 100 Ω 0805 (buzzer gate) | 1 |
| `R8` | 10 kΩ 0805 (buzzer gate pulldown) | 1 |
| `R9` | 1 kΩ 0805 (power LED) | 1 |
| `R10`–`R13` | 100 Ω 0805, ESC signal series | 4 |
| `F1` | PPTC 0.5 A hold, 1206 | 1 |
| — | Test points, 1 mm exposed pad | 8 |
| `J1` | JST-SH 1.0 mm 8-pin, SMD | 1 |
| `J2` | 1×15 female header 2.54 mm | 2 |
| `J3` | 1×7 male header 2.54 mm | 1 |
| `J4`, `J5` | JST-SH 1.0 mm 5-pin, SMD | 2 |
| `J6` | JST-SH 1.0 mm 4-pin, SMD | 1 |
| `J7`, `J8` | 1×2 male header 2.54 mm | 2 |
| `J9` | 1×4 male header 2.54 mm, spare GPIO | 1 |

0805 throughout rather than 0402 — this board is hand-soldered, and the space
saving is worth nothing here.

---

## Layout notes

- **Ground pour on both layers**, stitched with vias. It is a 2-layer board, so
  the bottom pour is the return path for everything; keep slots in it short.
- **Keep the `U1` input loop tight** — `C1` and `D1` right at the `J1` entry,
  short and fat traces to `U1 VIN`.
- **Route SPI away from the regulator.** `U1` is a switcher; the IMU's SPI bus
  and the two analog inputs are what you least want coupling into.
- **`BAT` traces:** 0.5 mm is generous for 0.4 A, but keep the clearance
  appropriate for 17 V, and keep the net physically short.
- **Analog inputs:** run `GPIO 34` and `GPIO 35` as short traces with their
  filter caps close to the devkit socket, over unbroken ground.
- Silkscreen every connector with its pin 1 marker and signal names, and put
  the board revision and date on the bottom. You will thank yourself in
  November.

## Open items before layout

- Frame clearance for the 44 × 56 mm outline.
- Physical positions for `J4`/`J5` cable runs to the ToF mounts — the sensors
  go front-left and rear-right of the CG, as widely spaced as the frame allows.
- Whether to fit `J8` at all, or jumper it permanently.
