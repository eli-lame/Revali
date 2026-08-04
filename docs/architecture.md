# Architecture

Revali is the flight controller firmware for a single vehicle: a quadrotor
hopcopter with a passive spring leg. It is not a general-purpose framework and
carries no abstractions for vehicles it does not fly. Interfaces exist for
exactly two reasons: to let a module be tested with no hardware attached, and
to keep the control loop from depending on a sensor datasheet.

## Two processors

```
   ┌──────────────────────────┐            ┌──────────────────────────────┐
   │   CONTROLLER  (ESP32)    │            │      VEHICLE  (ESP32)        │
   │                          │            │                              │
   │  Joystick ADC + button   │  ESP-NOW   │  Sensors → Estimator         │
   │        │                 │  ──────►   │      │                       │
   │  Input mapper            │  command   │  Mode Manager → Controller   │
   │        │                 │   50 Hz    │      │                       │
   │  Link TX ────────────────┼──          │  Mixer → Motors → ESCs       │
   │  Link RX ◄───────────────┼──          │      │                       │
   │        │                 │  ◄──────   │  Safety (can veto anything)  │
   │  Status LED / buzzer     │ telemetry  │                              │
   └──────────────────────────┘   10 Hz    └──────────────────────────────┘
```

The controller is deliberately dumb. It reads two analog axes and one button,
debounces them, maps them to normalized values, and ships them. **Every
decision that can hurt the vehicle is made on the vehicle** — arming, mode
legality, tilt limits, failsafes. A controller that crashes, browns out, or
walks out of range must not be able to command anything unsafe, and the vehicle
must react correctly to it simply going silent.

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
| **Mode Manager** | The flight state machine, joystick→setpoint mapping | PID internals |
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
