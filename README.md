# Charon

Flight controller firmware for a **hopcopter** — a quadrotor with a passive
spring-loaded telescopic leg that lets it hop along the ground instead of
flying continuously. Hopping is dramatically more power-efficient than hover,
because thrust is applied in short bursts at launch rather than held constantly.

The design replicates the Hopcopter described in *Science Robotics* (City
University of Hong Kong / HKUST): the leg is entirely passive — rubber bands
and a sliding rod — and **all** control authority comes from the four rotors.
The firmware's job is to keep the vehicle upright and steer it by tilting,
whether it is hovering, ballistic, or compressing on the ground.

## Hardware

| Role | Part |
|---|---|
| Vehicle MCU | ESP32-WROOM |
| IMU | SparkFun ICM-20948 (accel + gyro + mag) |
| Height / ground contact | 2× VL53L0X ToF, both downward-facing, spaced apart |
| Propulsion | 4× brushless motors + ESCs, quad-X layout |
| Controller MCU | ESP32-WROOM (offboard, USB-powered from the laptop) |
| Input | 1× SparkFun analog joystick (VCC, XOUT, YOUT, SEL, GND) |
| Link | ESP-NOW |
| Ground station | Browser dashboard, fed over the controller's USB cable |

The joystick commands **tilt only**. It never commands throttle directly —
altitude and hop energy are managed onboard. Clicking the joystick button
cycles what the two axes mean (see [flight_modes.md](docs/flight_modes.md)).

## Repository layout

```
firmware/     PlatformIO project — vehicle (flight controller)
controller/   PlatformIO project — offboard transmitter
shared/       Link protocol definitions included by both projects
groundstation/ Host bridge + browser dashboard
docs/         Design documents
tools/        Host-side scripts (log decoding, PID tuning, plotting)
```

## Documentation

Start with [architecture.md](docs/architecture.md), then
[hardware.md](docs/hardware.md).

| Doc | Covers |
|---|---|
| [architecture.md](docs/architecture.md) | Module boundaries and data flow |
| [hardware.md](docs/hardware.md) | Wiring, buses, pin map, the VL53L0X address problem |
| [data_model.md](docs/data_model.md) | The structs every module shares |
| [estimator.md](docs/estimator.md) | Attitude fusion and dual-ToF height |
| [control_loop.md](docs/control_loop.md) | Controller, mixer, motor output |
| [flight_modes.md](docs/flight_modes.md) | State machine and joystick mode cycling |
| [hop_mode.md](docs/hop_mode.md) | Hop phase machine and thrust profile |
| [communication.md](docs/communication.md) | ESP-NOW link, packet format, failsafe |
| [ground_station.md](docs/ground_station.md) | Browser dashboard for live telemetry and health |
| [safety.md](docs/safety.md) | Every failsafe and its trip condition |
| [scheduler.md](docs/scheduler.md) | Task rates and timing budget |
| [current_devtasks.md](docs/current_devtasks.md) | **The build order — start here to work** |

## Status

Early. The shared data contracts and estimator interfaces exist and compile;
no driver, controller, or link is implemented yet. See
[current_devtasks.md](docs/current_devtasks.md).

## Building

```bash
cd firmware && pio run -t upload && pio device monitor
```

## License

See [LICENSE](LICENSE).
