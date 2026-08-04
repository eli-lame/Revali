# Communication

## Link

**ESP-NOW** between the controller ESP32 and the vehicle ESP32. ESP-NOW is
Espressif's connectionless protocol built on the raw WiFi radio — no pairing,
no connection handshake, no association state. Each side just addresses a
packet to the other's MAC and sends it. That fits a real-time joystick link
better than a connection-oriented protocol: typical latency is ~1–4 ms
(vs. BLE's ~7.5 ms connection-interval floor, often more in practice), and if
a packet is lost there's no connection to re-establish — the next one just
goes out.

The vehicle registers the controller's MAC (and vice versa) as a peer at boot,
from a value stored in NVS — see **Pairing** below. Both sides send fixed-size
frames with `esp_now_send()`; there is no server/client asymmetry to design
around, unlike GATT.

| Stream | Direction | Rate |
|---|---|---|
| `COMMAND` | controller → vehicle | 50 Hz |
| `TELEMETRY` | vehicle → controller | 10 Hz |

Send and move on — do not wait on `esp_now_send()`'s delivery callback before
sending the next packet. For a stream where the next sample supersedes the
last, blocking on a per-packet delivery confirmation is pure added latency
with no benefit. Loss is handled by sequence numbers and the link-loss
timeout, not by waiting for acknowledgment.

### ILink: ESP-NOW is the target, everything else is a seam

**ESP-NOW is the transport being built.** It's the only one scheduled in
[current_devtasks.md](current_devtasks.md) (Phase 9) and the only one that
ships on the finished vehicle.

Two things sit behind the same `ILink` interface (`open`, `send`, `poll`,
`connected`, `last_rx_us`) purely so nothing above the transport has to be
rewritten if the transport changes:

- **`SerialLink`** — USB serial, built *first* (Phase 5), before ESP-NOW
  exists. Lets the mode manager, controller, and safety logic be brought up
  and tested on the bench over a cable, so the control loop and the radio
  stack aren't being debugged at the same time.
- **`BleLink`** — **not implemented, not scheduled.** A documented fallback
  only, kept in reserve for the one thing ESP-NOW can't do: talk to a generic
  phone or tablet with no dedicated controller hardware. Nothing in this
  project's plan needs that — the controller is a dedicated ESP32 — so it is
  not being built. The only reason it's mentioned here at all is so that *if*
  a phone-app control path is ever wanted, adding it is a new `ILink`
  implementation, not a redesign of everything above the transport.

## Framing

Same frame on every transport, so the parser is written and tested once.

```
┌────────┬──────┬─────┬────────┬─────────┬───────┐
│ MAGIC  │ TYPE │ SEQ │ LENGTH │ PAYLOAD │ CRC16 │
│ 2 B    │ 1 B  │ 1 B │  1 B   │  0-64 B │  2 B  │
└────────┴──────┴─────┴────────┴─────────┴───────┘
```

- `MAGIC` — `0xC4 0x0A`. Resynchronization point for the byte-stream
  transports; cheap sanity check on the packet ones.
- `SEQ` — increments per packet, wraps at 256. Detects loss and, critically,
  **detects reordering and duplication**. Drop any command whose sequence is
  older than the last accepted one.
- `CRC16` — CCITT, over `TYPE` through `PAYLOAD`. ESP-NOW's WiFi framing has
  its own CRC and the serial transport does not; validating at this layer
  regardless means one parser is trusted everywhere instead of relying on a
  transport-level guarantee that not every transport provides.

Reject silently and count the failure. Never act on a packet that fails any
check, and never let a malformed packet stall the parser — a receiver that can
be wedged by one bad byte is a receiver that fails permanently on the first
glitch.

## Packet types

### `CMD_CONTROL` — controller → vehicle, 50 Hz

The only packet that flies the vehicle.

| Field | Type | Meaning |
|---|---|---|
| `axis_x` | int16 | normalized −1000..1000, post-calibration, post-deadband |
| `axis_y` | int16 | normalized −1000..1000 |
| `axis_mode` | uint8 | `TILT` / `HEADING` / `ALTITUDE` |
| `request` | uint8 | `NONE` / `ARM` / `DISARM` / `LAND` / `HOP_TOGGLE` / `ESTOP` |
| `flags` | uint8 | bit 0: controller believes joystick is centred |

Sent **continuously at 50 Hz whether or not anything changed**. The stream is
the heartbeat; there is no separate keepalive. A gap in the stream *is* the
failsafe signal, and that only works if the stream is unconditional.

`request` is edge-triggered on the vehicle. The controller holds a request
value for ~100 ms (5 packets) so a single dropped packet does not lose a button
press, and the vehicle acts on the transition, not the level.

### `TLM_STATE` — vehicle → controller, 10 Hz

| Field | Type |
|---|---|
| `timestamp_ms` | uint32 |
| `vehicle_state` | uint8 |
| `axis_mode` | uint8 |
| `roll_deg`, `pitch_deg`, `yaw_deg` | int16 ×3 |
| `height_cm` | int16 |
| `battery_mv` | uint16 |
| `hop_phase` | uint8 |
| `status_flags` | uint16 |
| `rx_loss_pct` | uint8 |

`status_flags` carries armed, each failsafe's tripped bit, estimator health,
and ToF confidence. The controller uses these to drive its LED and buzzer — the
pilot must be able to tell armed from disarmed, and healthy from degraded,
without looking at a laptop.

### `CMD_PARAM_SET` / `CMD_PARAM_GET` / `TLM_PARAM` — tuning

PID gains, hop parameters, calibration values. Acknowledged, unlike control
packets — losing a gain change silently would be maddening. Refuse parameter
writes while armed, except for the small allowlist you actually need to tune
in flight.

### `TLM_LOG` — vehicle → host

High-rate log records, drained over USB serial. Do not attempt to stream 1 kHz
logs over ESP-NOW; buffer to a ring in RAM and dump after landing.

## Failsafe on link loss

The vehicle tracks `now - last_valid_command_us`:

| Elapsed | Response |
|---|---|
| > 150 ms | warn — status flag set, controller LED changes |
| > 300 ms | **`FAILSAFE`** — ignore stale input, level attitude, hold height |
| > 1 s | begin autonomous descent |
| touchdown | disarm |

Never fly on a stale command. A held-over tilt command with no one to cancel it
is a vehicle accelerating in a straight line into something. Zero the input,
level the attitude, and put it down.

If the vehicle is `HOPPING` when the link drops, **complete the current hop
cycle, then transition to `FLYING` and descend.** Cutting thrust mid-launch is
worse than finishing a hop that is already ballistic.

### Pairing

ESP-NOW has no discovery or advertisement to secure — a device simply
registers a peer MAC with `esp_now_add_peer()` and can then send to it, so the
binding has to be enforced explicitly:

- Store the counterpart's MAC address in NVS on each side. On boot, register
  **only** that MAC as a peer.
- Reject any incoming frame whose source MAC doesn't match the bound peer,
  even though ESP-NOW will hand you the sender's address on every receive
  callback — don't skip this check because "only the vehicle is listening."
  Anything on the same channel can send an ESP-NOW frame; there is no
  connection state stopping it.
- An explicit pairing gesture (e.g. hold `SEL` at boot) is required to learn a
  new peer MAC. Never auto-bind to the first sender seen.
- Optional hardening: ESP-NOW supports per-peer AES-CTR encryption
  (`esp_now_set_pmk` / a per-peer LMK). Worth adding once the unencrypted link
  is proven, cheap enough that there's little reason not to.

This costs almost nothing and removes an entire category of accident — a
second controller, or another ESP-NOW device entirely, being able to command
the vehicle.
