# Revali

Flight controller firmware for a **hopcopter** — a quadrotor with a passive
spring-loaded telescopic leg that hops along the ground instead of hovering.
The leg is entirely passive; all control authority comes from the four rotors.

## The docs are the source of truth

Design decisions live in `docs/`, not in this file and not in chat history.
Read the relevant one before changing anything it covers, and **update it in
the same change** when a decision moves.

| Doc | Authoritative for |
|---|---|
| `docs/architecture.md` | Module boundaries, data flow, layering |
| `docs/hardware.md` | **The pin map.** Wiring, buses, sensor quirks |
| `docs/hardware_roadmap.md` | Board architecture, the 4-in-1 stack, power topology |
| `docs/data_model.md` | The structs every module shares |
| `docs/estimator.md` | Attitude fusion, dual-ToF height |
| `docs/control_loop.md` | Controller, mixer, motor output |
| `docs/flight_modes.md` | State machine, per-controller intent resolution |
| `docs/hop_mode.md` | The hop cycle |
| `docs/safety.md` | Arming, failsafes, voltage thresholds |
| `docs/communication.md` | Link protocol and framing |
| `docs/current_devtasks.md` | Build order and task numbers (`[2.8]`, `[H.5]`, …) |

`docs/hardware.md` wins any disagreement about a GPIO number. If a pin must
change, change that table first, then everything referencing it.

## Two tracks

Work is split so neither blocks the other. A session normally belongs to one.

- **Firmware** — `firmware/`, `controller/`, `shared/`, `tools/`.
  Phases 0–13 in `current_devtasks.md`.
- **Hardware** — `hardware/`, and the hardware docs.
  Phase H in `current_devtasks.md`.

Firmware development runs on the existing breadboard and is never blocked
behind a PCB revision. That is deliberate — see `docs/hardware_roadmap.md`.

## Build and test

```bash
cd firmware && pio run -e esp32dev      # vehicle
cd firmware && pio test -e native       # host-side unit tests
cd controller && pio run -e esp32dev    # transmitter
```

`shared/link/` is included by both projects via `-I ../shared` so the wire
protocol is defined exactly once. Anything hardware-dependent must be excluded
from the `native` source filter in `firmware/platformio.ini` — `hal/` is the
seam that keeps that possible.

C++17. Arduino framework. `lib_ldf_mode = deep+`.

## Current hardware state

Breadboard: ESP32-WROOM-32 devkit, SparkFun ICM-20948 on **SPI**, two VL53L0X
on I2C, four discrete ESCs. Moving to a two-board stack — a Skystars KO50A
4-in-1 carrying all motor current, and a custom carrier board carrying only
signal-level electronics. Motor current never comes onto our board.

**ESP-NOW is the link for this hardware generation.** ELRS/CRSF is the eventual
target and is deliberately deferred until the board stops changing; the carrier
board reserves a UART footprint so that switch is a firmware change. Do not
propose migrating the link as part of unrelated work.

ESP-NOW runs on the WiFi radio, so **ADC2 is unusable** — every analog input
goes on ADC1.

## Standing rules

1. **Props off.** Until `current_devtasks.md` Phase 11 says otherwise, and for
   every bench and bring-up step regardless.
2. **Never skip a phase check.** The ordering exists so each failure is cheap
   and isolated. Nothing depends on something unproven.
3. **One variable at a time.** This applies to hardware changes as much as to
   PID tuning.
4. **Attitude control never stops** — not in any hop phase, not ever.
5. **Host-test everything that compiles on a host.** Mixer, PID, state
   machines, link parser. The cheapest bugs you will ever fix.
6. **Log before you need it.** You get one flight's worth of evidence.

## Conventions

- Drivers convert to SI units. Nothing downstream sees raw LSBs.
- Nothing in the 1 kHz loop calls `Serial.print` directly.
- Never read sensors inside an ISR — set a flag, read in the loop.
- Interfaces (`IImuSource`, etc.) exist so hardware can be swapped and faked;
  see `docs/architecture.md` before adding or bypassing one.
- Geometry constants (ToF baseline, CG offsets, leg length) belong in
  `Parameters`, never in code comments.
