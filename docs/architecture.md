# Architecture

Revali is the flight controller firmware for a single vehicle: a quadrotor
hopcopter with a passive spring leg. It is not a general-purpose framework and
carries no abstractions for vehicles it does not fly. Interfaces exist for
exactly two reasons: to let a module be tested with no hardware attached, and
to keep the control loop from depending on a sensor datasheet.

## Two processors

The vehicle always has a peer that turns human input into `CMD_CONTROL`
packets. There are two interchangeable forms of that peer, and **the vehicle
cannot tell them apart** — both send the identical intent packet (see
[communication.md](communication.md)):

**A — Reference joystick controller** (the replicable default):

```
   ┌──────────────────────────┐            ┌──────────────────────────────┐
   │   CONTROLLER  (ESP32)    │            │      VEHICLE  (ESP32)        │
   │                          │            │                              │
   │  Joystick ADC + button   │  ESP-NOW   │  Sensors → Estimator         │
   │        │                 │  ──────►   │      │                       │
   │  Interpret → intent      │  command   │  Setpoint → Controller       │
   │        │                 │   50 Hz    │      │                       │
   │  Link TX ────────────────┼──          │  Mixer → Motors → ESCs       │
   │  Link RX ◄───────────────┼──          │      │                       │
   │        │                 │  ◄──────   │  Safety (can veto anything)  │
   │  Status LED / buzzer     │ telemetry  │                              │
   └──────────────────────────┘   10 Hz    └──────────────────────────────┘
```

**B — Gamepad + ground dongle** (if you have a DS4-compatible controller):

```
   DS4 ──BT──► ┌───────────────────┐  ESP-NOW  ┌──────────────┐
   (gamepad)   │  DONGLE  (ESP32)  │ ────────► │   VEHICLE    │
               │  Bluepad32 pairs  │  command  │  (unchanged  │
               │  DS4, maps → intent│  50 Hz    │   from A)    │
               │        │          │ ◄──────── │              │
               │  Link  │  USB     │ telemetry └──────────────┘
               └────────┼──────────┘   10 Hz
                        ▼
                   LAPTOP (ground station, display-only)
```

In both forms the peer is **deliberately dumb about safety**. It reads input,
resolves it to intent (desired roll/pitch/yaw-rate/climb-rate), and ships it.
**Every decision that can hurt the vehicle is made on the vehicle** — arming,
command legality, tilt and height limits, failsafes. A controller that crashes,
browns out, or walks out of range must not be able to command anything unsafe,
and the vehicle must react correctly to it simply going silent.

What the peer is *not* dumb about is **interpretation**: turning sticks and
buttons into intent is device-specific (a one-stick joystick cycles modes to
fill four command fields; a two-stick gamepad fills them directly), so it lives
here, on the peer — never on the vehicle. That is the whole point of sending
intent rather than raw axes. In form B the dongle, not the laptop, generates
`CMD_CONTROL`: the flight-critical path is DS4 → dongle → vehicle, all
real-time hardware, and the laptop stays display-only per
[ground_station.md](ground_station.md).

## Vehicle data flow

```
        ICM-20948 (SPI, 1 kHz)          VL53L0X ×2 (I2C, ~50 Hz)      Battery ADC
                │                              │                          │
                ▼                              ▼                          ▼
            ImuData                        RangeData[2]              BatteryState
                └──────────────┬───────────────┘                          │
                               ▼                                          │
                        State Estimator                                   │
                 (Mahony attitude + height filter)                        │
                               │                                          │
                               ▼                                          │
                        VehicleState ─────────────────────────────────────┤
                               │                                          │
   ESP-NOW cmd ──► Mode Manager ──► DesiredState                          │
                        ▲      │                                          │
                        │      ▼                                          │
                        │   Controller (cascaded PID)                     │
                        │      │                                          │
                        │      ▼                                          │
                        │  ControlOutput  (torque + collective thrust)    │
                        │      │                                          │
                        │      ▼                                          │
                        │   Mixer (quad-X)                                │
                        │      │                                          │
                        │      ▼                                          │
                        │  MotorOutput                                    │
                        │      │                                          │
                        │      ▼                                          │
                        │  Motor Manager ──► ESCs                         │
                        │      ▲                                          │
                        └──────┴───────── Safety ◄─────────────────────────┘
                                    (arms, disarms, clamps, kills)
```

Safety sits **downstream of everything** and holds a veto. It can force
`MotorOutput` to idle or zero regardless of what the controller wanted, and it
can force the Mode Manager into `FAILSAFE`. No other module may write motor
outputs directly.

## Modules

| Module | Owns | Must not know about |
|---|---|---|
| **HAL** | GPIO, SPI, I2C, LEDC/RMT, ADC, timers | Anything about flight |
| **Drivers** | ICM-20948, VL53L0X, ESC output, battery sense, LEDs | Estimation or control math |
| **Estimator** | Attitude fusion, height fusion, ground-plane slope | Which chip produced a sample |
| **Mode Manager** | The flight state machine; scaling/clamping incoming intent into `DesiredState` | How input was interpreted, PID internals |
| **Hop Sequencer** | The hop phase machine and thrust profile | Motor wiring |
| **Controller** | Cascaded angle→rate→torque PID, altitude PID | Motor count or layout |
| **Mixer** | Quad-X allocation, saturation handling | Vehicle state |
| **Motor Manager** | Arming, idle, ESC protocol, output ordering | Why it was told to stop |
| **Link** | ESP-NOW transport, framing, CRC, sequence, timeout | What a command means |
| **Safety** | Every failsafe, the kill path | How to fly |
| **Parameters** | Persisted tuning values (NVS), defaults | Who consumes them |
| **Logging** | Ring buffer, flush, telemetry framing | Everything (read-only) |

## Layering rule

Dependencies point one direction only:

```
HAL ◄── Drivers ◄── Estimator ◄── Controller/Mixer ◄── Mode Manager
                         ▲                                  ▲
                         └──────── Safety ──────────────────┘
                                      ▲
                                     Link
```

A lower layer never includes a higher one. `Controller` includes
`core/types.h` and nothing else from the vehicle. This is what allows the PID
and mixer to be compiled and tested on a host machine with no ESP32 present —
which is how they will actually be tuned before the first flight.

## Why interfaces exist here

Only three interfaces are justified, and each earns its keep:

- **`IImuSource`** — lets the fusion filter run against recorded or synthetic
  samples on a host, and lets a bench build inject a fake IMU.
- **`IStateEstimator`** — lets the controller be exercised against a scripted
  state trajectory (e.g. "vehicle is tipping at 200 °/s") without flying.
- **`ILink`** — lets the vehicle be driven from USB serial during bring-up
  before ESP-NOW works, and keeps a BLE implementation available as a drop-in
  if a phone/tablet control path is ever wanted. See
  [communication.md](communication.md).

Everything else is a concrete class. Do not add an interface without a second
implementation that actually exists.
