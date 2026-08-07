# Ground station

A browser-based dashboard for watching the vehicle and the controller live:
health status of both, attitude, height, hop phase, stick position, link
quality, and failsafe state.

This is not a nice-to-have bolted on at the end. It is a **debugging
instrument**, and it is most valuable during tuning and hop bring-up — which
is why it lands in the build order *before* the control loop is closed, not
after. Tuning a PID or diagnosing a hop abort by squinting at scrolling serial
text is painful and slow; watching an attitude trace and a phase timeline is
not.

## Architecture

The controller is already USB-tethered to the laptop for power (see
[hardware.md](hardware.md)), so the data path is essentially free:

```
   VEHICLE  ──ESP-NOW──►  CONTROLLER  ──USB serial──►  BRIDGE  ──WebSocket──►  BROWSER
   (telemetry 10 Hz)      or DONGLE                   (host process,          (dashboard)
                          (adds its own               Python or Node)
                           input state)
```

"Controller" here is whichever peer you built — the reference joystick ESP32,
or the DS4/SCUF **dongle** (the gamepad pairs to the dongle, not the laptop;
see [architecture.md](architecture.md)). Either way it already receives
`TLM_STATE` from the vehicle and already knows its own input state, and it
forwards both up the USB cable it is already plugged into. A small host-side
bridge process reads that serial stream, decodes the frames, and re-serves
them to a browser over a WebSocket.

Note the dongle, **not** the laptop, generates `CMD_CONTROL`: this USB link is
telemetry-and-config only, never the command path, so the display-only
guarantee below holds even in the gamepad configuration.

### Why not host the webapp on the vehicle ESP32

The obvious-looking alternative — run a web server on the vehicle and connect
the browser to it over WiFi — is worse here, for a specific reason:

**ESP-NOW rides on the WiFi radio.** Running a SoftAP or joining a network
alongside ESP-NOW forces both onto the same channel and adds the WiFi stack's
scheduling to the same core that already has to service the control link.
That is a real risk to control-link latency and jitter — and per
[scheduler.md](scheduler.md), radio jitter is the thing that most threatens
the attitude loop. It also means the flight-critical link and a debugging
convenience share a failure domain, which is exactly backwards.

The USB path costs the vehicle **nothing**. No extra radio, no extra CPU, no
new failure mode. The vehicle keeps doing exactly what it already does — send
`TLM_STATE` at 10 Hz — and everything else happens off-board.

### Why a host bridge rather than the browser reading serial directly

Browsers can talk to serial ports via the Web Serial API, which would remove
the bridge process. It is worth knowing why the bridge is still the better
default:

- Web Serial is Chromium-only and requires a user gesture per connection.
- The bridge can log every frame to disk for later analysis, which the
  blackbox tooling wants anyway.
- The bridge can serve historical data on reconnect, so refreshing the page
  does not lose your session.
- It keeps frame decoding in one place — reuse the same packet definitions
  from `shared/link/`, rather than reimplementing the parser in JavaScript.

A Web Serial fallback is reasonable to add later if the bridge process becomes
annoying to run.

## What the dashboard shows

### Vehicle health

| Panel | Source |
|---|---|
| Vehicle state (`DISARMED`/`ARMED`/`FLYING`/`HOPPING`/`FAILSAFE`/…) | `TLM_STATE.vehicle_state` |
| Which failsafes are tripped, individually | `TLM_STATE.status_flags` |
| Battery voltage, with the warn/land/critical thresholds marked | `TLM_STATE.battery_mv` |
| Estimator health + `height_confidence` (`NONE`/`INERTIAL`/`SINGLE`/`BOTH`) | `status_flags` |
| Arming blockers — *which* precondition is currently failing | `status_flags` |

That last one deserves emphasis. [safety.md](safety.md) already requires the
vehicle to report which arming check failed, because "won't arm" with no
reason is the most frustrating possible failure mode. The dashboard is where
that reason should be impossible to miss.

### Attitude and motion

- A 3D or artificial-horizon view driven by roll/pitch/yaw.
- Rolling time-series plots of roll, pitch, yaw, height, and vertical
  velocity. Time-series matter more than the 3D view for tuning — oscillation
  and overshoot are obvious in a trace and invisible in an orientation cube.
- Height plot with `ground_contact` shaded, so touchdown/liftoff is visible at
  a glance.

### Controller state

- The four resolved intent fields (`roll_cmd`/`pitch_cmd`/`yaw_rate_cmd`/
  `climb_rate_cmd`) as the controller is actually sending them.
- Raw input underneath that intent, device-appropriate: the joystick's stick
  dot with deadband drawn and its current control scheme
  (`TILT`/`HEADING`/`ALTITUDE`); or, for the dongle, both DS4 sticks and the
  live button states.
- Last `request` emitted.

Showing both the raw input *and* the intent it resolved to is the point. A
miscalibrated joystick centre, a wrong deadband, or a gamepad axis mapped
backwards all become immediately visible — you see the intent diverge from the
raw input, instead of inferring it from the vehicle drifting. This data comes
from the controller/dongle's own USB channel, not from `CMD_CONTROL` (which no
longer carries raw axes).

### Link quality

- Packets/sec received on both legs, and `rx_loss_pct`.
- Time since last valid command, drawn against the 150 ms / 300 ms / 1 s
  failsafe ladder from [communication.md](communication.md).
- CRC error and sequence-gap counters.

### Hop telemetry

- Current `hop_phase`, with a timeline of recent phase transitions.
- Hop count, last apex height vs. target.
- Last abort reason.

A phase timeline aligned against the height and vertical-velocity traces is
the single most useful view for hop tuning — it is how you see whether the
launch burst is actually firing at extension or missing the window.

## Design constraints

**Read-only for anything flight-critical.** The dashboard displays; it does
not fly. Arming, mode changes, and emergency stop stay on the physical
controller, where the pilot's hand already is. A browser tab is not an
acceptable place to put a kill switch — it can be behind another window,
frozen, or on a laptop that just went to sleep.

Parameter tuning (PID gains, hop values) is the one reasonable exception, and
only through the existing `CMD_PARAM_SET` path with its existing rules:
acknowledged, refused while armed except for the explicit in-flight allowlist.

**Never let the dashboard slow the vehicle.** All of this is downstream of a
10 Hz telemetry stream that the vehicle already sends. Nothing on the vehicle
changes to support it. If the bridge or browser falls behind, it drops
frames — it does not apply backpressure to anything airborne.

**Log everything to disk as it arrives.** The bridge should write to disk
regardless of whether a browser is connected. Per
[current_devtasks.md](current_devtasks.md), you get one flight's worth of
evidence after a crash and no ability to reproduce it — a dashboard that only
shows live data and keeps no history throws that away.

**Session boundary: one log file per arm/disarm cycle.** The bridge watches
`TLM_STATE.vehicle_state` and opens a new timestamped file the moment it sees
a transition into `ARMED`, closing it on the transition back to `DISARMED`
(including via a failsafe-driven landing). This makes each flight trivially
its own file for the blackbox tooling — no need to slice a continuous log by
timestamp after the fact to find where one flight ends and the next begins.

Traffic between flights — bridge idle, vehicle disarmed on the bench — is not
logged. If pre-arm telemetry ever turns out to matter (e.g. diagnosing a
refused arm), that is a reason to widen the boundary, not a reason to log
continuously by default.

## Suggested stack

Deliberately boring, because this is a tool and not the point of the project:

- **Bridge**: Python (`pyserial` + `websockets`) or Node (`serialport` + `ws`).
  Decode using the definitions in `shared/link/` so the frame format stays
  defined exactly once.
- **Frontend**: a single static page. Plain HTML/JS with a small charting
  library is entirely sufficient; a build step and a framework are not
  required for a handful of live plots and status panels.
- Serve it from the bridge process itself so starting the ground station is
  one command.
