# Communication

## Link

**ESP-NOW** between the controller ESP32 and the vehicle ESP32. ESP-NOW is
Espressif's connectionless protocol built on the raw WiFi radio — no pairing,
no connection handshake, no association state. Each side just addresses a
packet to the other's MAC and sends it. That fits a real-time control link
better than a connection-oriented protocol: typical latency is ~1–4 ms
(vs. BLE's ~7.5 ms connection-interval floor, often more in practice), and if
a packet is lost there's no connection to re-establish — the next one just
goes out.

Here "the controller ESP32" means whichever peer you built — the reference
joystick controller *or* the DS4/SCUF ground dongle. Both are ESP32s that speak
ESP-NOW to the vehicle, and the vehicle cannot tell them apart (see
[architecture.md](architecture.md)). A DS4 gamepad connects to the *dongle*
over Bluetooth Classic; it never touches this ESP-NOW link.

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
  only, kept in reserve for the one thing ESP-NOW can't do: let a generic phone
  or tablet *be the controller*, with no dedicated hardware on the ground at
  all. (This is unrelated to the DS4 gamepad option — that gamepad reaches the
  vehicle through the dongle's ESP-NOW link, not over BLE. See
  [architecture.md](architecture.md).) Nothing in the plan needs a phone-as-
  controller path, so this isn't being built; it's noted only so that if it is
  ever wanted, it's a new `ILink` implementation rather than a redesign of
  everything above the transport.

## The gamepad link (DS4 → dongle)

Applies only to the gamepad controller (form B in
[architecture.md](architecture.md)). The reference joystick has no such link —
it reads its stick straight off an ADC.

**Transport: Bluetooth Classic HID, handled by [Bluepad32](https://bluepad32.readthedocs.io/)
on the dongle.** The dongle is the Bluetooth host; the DS4/SCUF is a standard
HID gamepad peripheral. We do **not** design this protocol — the controller
defines its HID reports and Bluepad32 decodes them into stick and button state.
None of the `MAGIC`/`SEQ`/`CRC16` framing below applies to it; that framing is
for *our* protocol (the dongle↔vehicle ESP-NOW link and the serial bring-up
link).

**What it carries: raw gamepad state** — two analog sticks, triggers, buttons.
*Not* intent. The dongle reads this state and **generates** `CMD_CONTROL` from
it, applying the mapping in [flight_modes.md](flight_modes.md). Interpretation
lives on the dongle; this link sits upstream of it. The DS4 never emits an
intent packet and never touches ESP-NOW.

Characteristics to design around:

- **Range ~10 m.** Bluetooth Classic is couch-to-console range. Because the
  dongle sits by the laptop, *you* must stay within ~10 m of the ground
  station. Fine for a hopcopter working near you; a real constraint to know
  going in. (The vehicle's ESP-NOW range is far longer — the gamepad hop is now
  the limiting leg.)
- **Latency ~4–11 ms** per HID report, on top of the ESP-NOW leg. Acceptable
  for tilt commands, but it stacks — measure end-to-end as part of the
  coexistence check.
- **Radio coexistence.** The dongle runs Bluetooth Classic *and* ESP-NOW on one
  chip. This is the single real risk in the gamepad path: verify the 50 Hz
  command stream holds its rate and latency with the DS4 link active
  (`[6.18]` in [current_devtasks.md](current_devtasks.md)).

**Binding:** pair exactly one DS4, stored on the dongle, and do not auto-accept
the first gamepad that advertises — same philosophy as the ESP-NOW **Pairing**
section below. An unbound controller must not be able to fly the vehicle.

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

The only packet that flies the vehicle. It carries **intent**, not raw stick
positions: each field has one fixed meaning, already resolved by the
controller. The vehicle never learns what kind of device produced it — a
one-stick joystick doing mode-cycling and a two-stick gamepad commanding
everything at once emit the identical packet.

| Field | Type | Meaning |
|---|---|---|
| `roll_cmd` | int16 | desired roll, −1000..1000 = −max..+max tilt |
| `pitch_cmd` | int16 | desired pitch, −1000..1000 = −max..+max tilt |
| `yaw_rate_cmd` | int16 | desired yaw **rate**, −1000..1000 = −max..+max |
| `climb_rate_cmd` | int16 | desired climb **rate**, −1000..1000; **0 = hold height** |
| `request` | uint8 | `NONE` / `ARM` / `DISARM` / `LAND` / `SET_HOP` / `SET_HOVER` / `ESTOP` |
| `flags` | uint8 | bit 0: controller has a known-neutral input this tick |

The four command fields are **normalized fractions of the vehicle's configured
maxima**, not physical angles or rates. The controller says "70% of max roll,
zero yaw rate, 20% climb"; the vehicle scales each against its own
`max_tilt` / `max_yaw_rate` / `max_climb_rate` parameters and **clamps**. This
is deliberate: every physical limit — and therefore every safety envelope —
stays on the vehicle, exactly where [safety.md](safety.md) already puts the
attitude and height limits. A miscalibrated or misbehaving controller cannot
command past them.

`climb_rate_cmd = 0` means *hold current height*, not *cut thrust*. The
vehicle integrates the commanded rate against its own height estimate into a
held target (see `DesiredState` in [data_model.md](data_model.md)). The
joystick never has direct authority over collective thrust — that property is
preserved verbatim from the original design.

Sent **continuously at 50 Hz whether or not anything changed**. The stream is
the heartbeat; there is no separate keepalive. A gap in the stream *is* the
failsafe signal, and that only works if the stream is unconditional.

`request` is edge-triggered on the vehicle. The controller holds a request
value for ~100 ms (5 packets) so a single dropped packet does not lose a button
press, and the vehicle acts on the transition, not the level.

`SET_HOP` and `SET_HOVER` are **explicit, idempotent** states, deliberately not
a single toggle. Asking for a state the vehicle is already in is a harmless
no-op; there is no way for a mis-fire to flip you the wrong way. A two-stick
gamepad puts these on separate buttons; the one-stick joystick offers a single
toggle *gesture* that its firmware resolves — using the vehicle state it reads
from `TLM_STATE` — into whichever of the two explicit requests is correct. Both
send the same two request values.

Adjusting the hop **height target** is not a request — it goes through
`CMD_PARAM_SET` on `hop_height_target`, which must therefore be on the
refuse-while-armed in-flight allowlist. Requests are for discrete state
transitions; continuously-tunable values are parameters.

**Why intent and not raw axes.** The interpretation of "what the pilot wants"
is a property of the *input device*, not the aircraft — it depends on how many
sticks and buttons that device has. Putting it on the wire as intent keeps
that device-specific logic on the controller and leaves the vehicle's
command-handling uniform and device-agnostic. Adding a new controller type
never touches flight-critical firmware. See
[flight_modes.md](flight_modes.md) for how each device fills these four
fields.

### `TLM_STATE` — vehicle → controller, 10 Hz

| Field | Type |
|---|---|
| `timestamp_ms` | uint32 |
| `vehicle_state` | uint8 |
| `roll_deg`, `pitch_deg`, `yaw_deg` | int16 ×3 |
| `height_cm` | int16 |
| `battery_mv` | uint16 |
| `hop_phase` | uint8 |
| `status_flags` | uint16 |
| `rx_loss_pct` | uint8 |

There is no `axis_mode` field here anymore — the vehicle has no notion of axis
modes. If a controller has an internal control-scheme mode (the one-stick
joystick does; a gamepad does not), that is the controller's own state, known
to it locally, and never round-tripped through the vehicle.

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

### Two links, two ways to lose the pilot (gamepad only)

With the gamepad, the command path is two hops — DS4 → dongle → vehicle — so
there are two independent places it can break, and the ladder above only sees
the second one. The DS4 → dongle hop can drop on its own: gamepad powered off,
walked out of ~10 m range, or a dead controller battery.

When that happens the dongle has lost its input, and the rule is the same one
that governs the whole system: **never keep streaming a command nobody is
holding.** The dongle must **not** repeat the last stick values — a frozen tilt
with no hand behind it is exactly the stuck-command hazard the failsafe exists
to prevent. Instead the dongle **stops emitting `CMD_CONTROL`**, which the
vehicle sees as ordinary command-stream loss and handles with the identical
150 ms → 300 ms → descend ladder. Reusing the vehicle's existing failsafe for a
controller-side failure is deliberate: one well-tested safety path, not two.

The dongle should also flag "gamepad lost" to the ground station so you can see
*which* hop broke — but it does not need to tell the vehicle anything special.
Silence is already the correct signal.

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
