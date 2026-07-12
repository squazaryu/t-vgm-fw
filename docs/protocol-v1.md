# TumoVGM Protocol v1

This document is the normative wire contract between a Tumoflip client and the
RP2040 firmware in the Flipper Zero Video Game Module. Version 1.0 defines the
framing and lifecycle. Version 1.1 adds queryable device identity and the UART
binding used by the native Tumoflip client. Version 1.2 adds bounded IMU
telemetry and debounced gesture events.

The key words MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are interpreted as in
RFC 2119.

## Byte order and limits

All multi-byte integers are unsigned and little-endian. A receiver MUST NOT
allocate memory based on an unvalidated field from the wire. Version 1 limits a
payload to 512 bytes and a complete frame to 528 bytes.

| Offset | Size | Field | Requirement |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `TVG1` |
| 4 | 1 | Protocol major | `1` for this contract |
| 5 | 1 | Protocol minor | `2` for the current contract |
| 6 | 1 | Kind | Request `1`, response `2`, event `3`, error `4` |
| 7 | 1 | Flags | `MORE=0x01`, `ACK_REQUIRED=0x02` |
| 8 | 2 | Sequence | Nonzero for requests and replies; zero for events |
| 10 | 2 | Message ID | Nonzero; known IDs are listed below |
| 12 | 2 | Payload length | `0..512` |
| 14 | N | Payload | Message-specific data |
| 14+N | 2 | CRC | CRC-16/CCITT-FALSE |

CRC parameters are polynomial `0x1021`, initial value `0xFFFF`, no reflection,
and no final XOR. The covered bytes start at protocol major (offset 4) and end
at the last payload byte. Magic and the CRC field itself are excluded. The
check value for ASCII `123456789` is `0x29B1`.

A receiver MUST reject unknown flag bits, invalid kind/sequence combinations,
zero message IDs, bad CRC, and oversized payloads. It MUST accept a
syntactically valid unknown nonzero message ID so that the dispatcher can reply
with `UNSUPPORTED_MESSAGE`; an unknown ID must not desynchronize the stream.

## Version negotiation

The first valid request after transport connection MUST be `HELLO`. No session
or feature command is allowed before a successful HELLO response.

- A different major version MUST fail closed with `UNSUPPORTED_VERSION`, after
  which the responder closes or resets the transport session.
- Peers with the same major negotiate `min(local minor, peer minor)`.
- Peers negotiate `min(local max payload, peer max payload)`.
- A zero advertised max payload is malformed.
- New messages introduced by a later minor version receive
  `UNSUPPORTED_MESSAGE` when the older peer does not implement them.

### HELLO (`0x0001`)

Request payload (12 bytes):

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Role: Flipper `1`, VGM `2`, host diagnostic `3` |
| 1 | 1 | Reserved, MUST be zero |
| 2 | 2 | Maximum accepted payload |
| 4 | 8 | Requested capability mask |

Response payload (12 bytes): role, negotiated minor, negotiated max payload,
and available capability mask in the same field widths.

### CAPABILITIES (`0x0002`)

The response payload is 24 bytes: available capabilities `u64`, active
capabilities `u64`, max payload `u16`, max stream chunk `u16`, max queued stream
frames `u16`, and one reserved zero `u16`. Capability bits are generated from
`protocol/protocol-v1.json` and currently cover Video Out, IMU, GPIO capture,
hardware trigger, trace buffer, UART/SPI/I2C decoders, USB device, and USB host.

### DEVICE_INFO (`0x0007`, since v1.1)

The request payload is empty. The response is a fixed 48-byte identity record:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Hardware target; VGM RP2040 is `1` |
| 2 | 2 | Hardware revision, or `0` when not detectable |
| 4 | 24 | Zero-padded ASCII firmware version |
| 28 | 12 | Lowercase ASCII Git commit prefix, no terminator |
| 40 | 1 | Dirty build flag, `0` or `1` |
| 41 | 7 | Reserved, MUST be zero |

A v1.0 peer does not send this request and displays identity as unavailable.
The message does not disclose unique hardware identifiers.

## UART binding

The Video Game Module endpoint uses RP2040 `UART0` on GPIO0 TX and GPIO1 RX at
230400 baud, 8 data bits, no parity, one stop bit, and no hardware flow control.
These are the same physical lines used by the stock Expansion protocol. A
Tumoflip client MUST disable the Expansion service before acquiring USART and
MUST release USART and restore the service on every exit path.

Custom TumoVGM firmware does not emit the stock 9600-baud presence pulse. A
client probes TumoVGM at 230400 first; if HELLO times out it may listen at 9600
for the stock module pulse. Absence of both responses means missing or
unpowered hardware. Arbitrary bytes at either baud are never treated as a
compatible module.

## Request lifecycle

Every request gets exactly one terminal response or error with the same
nonzero sequence. Sequence values MAY be reused only after their previous
request is terminal. A requester owns its timeout; on timeout it SHOULD send a
`CANCEL`, then discard a late terminal reply for that sequence.

`CANCEL (0x0005)` payload is `session_id u32, target_sequence u16, reserved
u16`. Cancellation is idempotent. A completed or already-cancelled target
returns a successful terminal response. A command that cannot be interrupted
MAY finish normally, but MUST still release all owned resources.

`PING (0x0006)` contains an opaque `u64` token and echoes it unchanged.

An error frame payload starts with `code u16, detail u16`. It MAY append at most
96 bytes of diagnostic UTF-8. Diagnostic text is not a stable API. Error codes
are `UNSUPPORTED_VERSION`, `UNSUPPORTED_MESSAGE`, `MALFORMED`, `BAD_STATE`,
`BUSY`, `TIMEOUT`, `CANCELLED`, `NO_CAPABILITY`, `OVERFLOW`, and `INTERNAL`.

## Session ownership

Only one mutable feature session can exist at a time:

```text
DISCONNECTED -> NEGOTIATING -> READY -> ACTIVE -> READY
       ^                                    |
       +------------- disconnect -----------+
```

`SESSION_OPEN (0x0003)` request payload is `requested_caps u64, timeout_ms
u32`. Its response is `session_id u32, granted_caps u64, lease_ms u32`. Session
IDs are nonzero and assigned by the VGM.

`SESSION_CLOSE (0x0004)` carries `session_id u32`. Only the owner may close an
active session. The first valid close releases resources and succeeds. A
duplicate close for the most recently closed ID also succeeds and is reported
as already closed internally. A close for another ID fails with `BAD_STATE`.

Transport disconnect atomically cancels outstanding work, stops streams,
returns externally visible GPIO to its safe state, releases the active session,
and enters `DISCONNECTED`. Repeating cleanup after disconnect has no effect.

## Streaming and backpressure

Streaming is credit based. `STREAM_CREDIT (0x0010)` payload is `session_id u32,
stream_id u16, credits u16`. Each `STREAM_DATA (0x0011)` event consumes exactly
one credit and contains `session_id u32, stream_id u16, chunk_sequence u16,
data...`. The producer MUST pause at zero credits.

Each stream has a statically bounded queue advertised by CAPABILITIES. Queue or
credit counter overflow fails with `OVERFLOW`; it must never wrap. Dropping
data, if a feature permits it, MUST be observable in the terminal
`STREAM_END (0x0012)` status. `STREAM_END` contains `session_id u32, stream_id
u16, status u16` and does not consume credit.

## IMU service (since v1.2)

The ICM-42688-P service is available only when capability bit `IMU` is present,
an active session has been granted that capability, and the peer negotiated
minor version 2 or newer. SPI ownership is session-scoped; closing or expiring
the session powers down the sensor and returns its pins to input/high impedance.

`IMU_INFO (0x0020)` has an empty request. Its 12-byte response is `health u8,
who_am_i u8, bus_error u16, max_rate_hz u16, supported_rates u16, sample_format
u16, reserved u16`. `WHO_AM_I=0x47`; rate mask bits 0, 1, and 2 represent 10,
25, and 50 Hz. Sample format 1 is the fixed-point record below.

`IMU_CONFIG (0x0021)` request payload is `session_id u32, requested_rate_hz
u16, flags u8, reserved u8`. Flag bit 0 enables raw samples and bit 1 enables
gesture events. The responder clamps the requested rate to 10, 25, or 50 Hz
without exceeding the request. Its 12-byte response is `session_id u32,
actual_rate_hz u16, flags u8, stream_id u8, period_us u32`. Stream ID 1 is the
IMU sample stream.

IMU samples use `STREAM_DATA (0x0011)` and consume one granted credit. The
28-byte payload is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Session ID |
| 4 | 2 | Stream ID (`1`) |
| 6 | 2 | Sample sequence |
| 8 | 4 | Monotonic timestamp, ms |
| 12 | 2 | Temperature, signed centi-degrees Celsius |
| 14 | 6 | Signed acceleration X/Y/Z, mg |
| 20 | 6 | Signed angular velocity X/Y/Z, deci-degrees/s |
| 26 | 1 | Calibrated dominant-axis orientation |
| 27 | 1 | Health/calibration flags |

`IMU_GESTURE (0x0022)` is a zero-sequence event with a 16-byte payload:
`session_id u32, event_sequence u16, gesture u8, confidence u8, timestamp_ms
u32, orientation u8, reserved[3]`. Orientation changes require six stable
samples, at least 800 mg on the leading axis, and a 180 mg lead over the next
axis. The current axis is retained down to 550 mg around diagonal positions.
Gesture events have an 800 ms minimum debounce and are never emitted
outside an active session. They do not consume raw-stream credit, but only one
gesture may be pending at a time.

## Parser recovery and security

- A partial magic or frame is retained until more bytes arrive.
- Garbage before a possible `TVG1` prefix is discarded without reading beyond
  the supplied buffer.
- Bad CRC and oversize headers discard at least one byte before rescan.
- The codec stores only views into caller-owned buffers and performs no heap
  allocation.
- Feature dispatch MUST validate the exact payload length for its message before
  reading message fields.
- Hardware-driving capabilities remain disabled until HELLO, SESSION_OPEN, and
  explicit capability authorization all succeed.

The canonical machine-readable values are in
`protocol/protocol-v1.json`. Regenerate `include/tumovgm/protocol_ids.h` with:

```sh
python3 tools/generate_protocol_constants.py
```
