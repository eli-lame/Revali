#pragma once
//
// shared/link/protocol.h — the wire format for the Revali control and
// telemetry link.
//
// Included by BOTH the vehicle firmware and the controller firmware
// (`-I ../shared` in each platformio.ini), so the frame layout is defined
// exactly once. See docs/communication.md for the full design rationale —
// this header is the concrete encoding of that design, not a restatement of
// it.
//
// Plain C++17, zero Arduino/ESP32 dependency. Must compile under the `native`
// PlatformIO environment with no hardware present — keep it that way, since
// the link parser is meant to be host-tested (see current_devtasks.md Phase
// 5, task [5.3]).
//
// Scope note: this header defines the frame header, the fully-specified
// packet payloads (CMD_CONTROL, TLM_STATE), and the CRC16 utility the future
// framer will use. CMD_PARAM_SET/CMD_PARAM_GET/TLM_PARAM/TLM_LOG have
// reserved type IDs below but no payload layout yet — that is real protocol
// design not yet done (see communication.md), not an oversight. Frame
// encode/decode logic itself belongs in Phase 5 [5.2], not here — this file
// is definitions only.

#include <cstddef>
#include <cstdint>

namespace revali {
namespace link {

// ---------------------------------------------------------------------------
// Frame header
// ---------------------------------------------------------------------------
// MAGIC(2) TYPE(1) SEQ(1) LENGTH(1) PAYLOAD(0..kMaxPayloadBytes) CRC16(2)
// See docs/communication.md "Framing".

inline constexpr uint8_t kMagic0 = 0xC4;
inline constexpr uint8_t kMagic1 = 0x0A;

inline constexpr size_t kHeaderBytes = 4;   // MAGIC(2) + TYPE(1) + SEQ(1)
inline constexpr size_t kLengthFieldBytes = 1;
inline constexpr size_t kCrcBytes = 2;
inline constexpr size_t kMaxPayloadBytes = 64;
inline constexpr size_t kMaxFrameBytes =
    kHeaderBytes + kLengthFieldBytes + kMaxPayloadBytes + kCrcBytes;

// Packet type — the frame's TYPE field. Values are part of the wire contract;
// never renumber an existing entry, only append.
enum class PacketType : uint8_t {
    CmdControl = 0x01,   // controller -> vehicle, 50 Hz
    TlmState = 0x02,     // vehicle -> controller, 10 Hz
    CmdParamSet = 0x03,  // reserved — payload not yet specified
    CmdParamGet = 0x04,  // reserved — payload not yet specified
    TlmParam = 0x05,     // reserved — payload not yet specified
    TlmLog = 0x06,       // reserved — payload not yet specified
};

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no input/output reflection,
// xorout 0x0000. Check value for ASCII "123456789" is 0x29B1 — see
// firmware/test/test_protocol for the verifying test. Naming the exact
// variant matters: "CCITT" alone is ambiguous across several incompatible
// CRC-16 variants, and a mismatch between sender and receiver fails silently.
inline uint16_t crc16_ccitt(const uint8_t* data, size_t len,
                             uint16_t crc = 0xFFFF) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// CMD_CONTROL — controller -> vehicle, 50 Hz
// ---------------------------------------------------------------------------
// Carries resolved INTENT, not raw stick axes. See communication.md's
// "why intent and not raw axes" — the vehicle scales these against its own
// configured maxima and clamps; every physical/safety limit stays on the
// vehicle regardless of which controller produced this packet.

// `request` — edge-triggered on the vehicle; held ~100 ms (5 packets) by the
// controller so one dropped packet doesn't lose a button press. SET_HOP and
// SET_HOVER are deliberately explicit and idempotent, not a toggle — see
// communication.md.
enum class ControlRequest : uint8_t {
    None = 0,
    Arm,
    Disarm,
    Land,
    SetHop,
    SetHover,
    Estop,
};

// bit 0 of CmdControl::flags.
inline constexpr uint8_t kFlagKnownNeutral = 1 << 0;

struct CmdControl {
    int16_t roll_cmd = 0;        // -1000..1000 = -max..+max roll tilt
    int16_t pitch_cmd = 0;       // -1000..1000 = -max..+max pitch tilt
    int16_t yaw_rate_cmd = 0;    // -1000..1000 = -max..+max yaw rate
    int16_t climb_rate_cmd = 0;  // -1000..1000 = -max..+max climb rate; 0 = hold height
    uint8_t request = static_cast<uint8_t>(ControlRequest::None);
    uint8_t flags = 0;
} __attribute__((packed));

static_assert(sizeof(CmdControl) == 10, "CMD_CONTROL wire size changed");

// ---------------------------------------------------------------------------
// TLM_STATE — vehicle -> controller, 10 Hz
// ---------------------------------------------------------------------------

// Mirrors the vehicle state machine in docs/flight_modes.md. Deliberately a
// separate wire-level enum from any internal firmware enum — the wire format
// must stay stable even if the internal representation changes.
enum class VehicleState : uint8_t {
    Boot = 0,
    Initializing,
    Fault,
    Disarmed,
    Armed,
    Flying,
    Hopping,
    Landing,
    Failsafe,
    EmergencyStop,
};

// Mirrors HopPhase in firmware/include/core/types.h — kept as its own wire
// enum for the same reason as VehicleState above.
enum class HopPhase : uint8_t {
    Stance = 0,
    Launch,
    Ascent,
    Apex,
    Descent,
};

struct TlmState {
    uint32_t timestamp_ms = 0;
    uint8_t vehicle_state = static_cast<uint8_t>(VehicleState::Boot);
    int16_t roll_deg = 0;
    int16_t pitch_deg = 0;
    int16_t yaw_deg = 0;
    int16_t height_cm = 0;
    uint16_t battery_mv = 0;
    uint8_t hop_phase = static_cast<uint8_t>(HopPhase::Stance);
    // Bit layout not yet assigned — armed/failsafe/estimator-health/ToF
    // confidence bits land here in Phase 9 (safety.md). Treat as opaque until
    // then; do not invent bit positions ahead of that design work.
    uint16_t status_flags = 0;
    uint8_t rx_loss_pct = 0;
} __attribute__((packed));

static_assert(sizeof(TlmState) == 19, "TLM_STATE wire size changed");

}  // namespace link
}  // namespace revali
