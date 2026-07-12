# Architecture

## Product Boundary

`t-vgm-fw` runs on the RP2040 inside the official Flipper Zero Video Game
Module. It is a separate firmware product from Tumoflip, which runs on the
Flipper Zero STM32WB55.

The RP2040 is responsible for workloads that benefit from PIO, DMA, external
Video Out, or independent buffering. The Flipper remains responsible for user
authorization, session ownership, SD persistence, and access to its native
radio and NFC subsystems.

## Components

### RP2040 firmware

- Hardware abstraction for Video Out, IMU, GPIO, USB, PIO, and DMA.
- A bounded scheduler with explicit ownership of each peripheral.
- A framed transport endpoint with capability negotiation.
- No persistent secrets in the initial development line.

### Tumoflip client

- Detects the module and reads firmware identity and capabilities.
- Starts and stops explicit sessions.
- Presents human-readable status and errors.
- Rejects incompatible protocol major versions before issuing commands.

### Companion integration

- Discovers signed or checksum-verified UF2 release artifacts.
- Shows installed and available versions before installation.
- Never flashes an incompatible hardware target.
- Preserves an explicit stock-recovery path.

## Transport Contract

The transport will use a small binary frame instead of ad-hoc text commands.
The initial frame fields are:

| Field | Purpose |
| --- | --- |
| Magic | Reject unrelated or misaligned data. |
| Protocol major/minor | Enforce compatibility. |
| Message type | Identify request, response, event, or error. |
| Sequence | Match requests and responses. |
| Payload length | Bound allocation and parsing. |
| Payload | Message-specific data. |
| CRC | Detect transport corruption. |

Every variable-length field must have a compile-time maximum. Unknown messages
must return a structured unsupported-message error without resetting either
device.

## Capability Negotiation

The first successful exchange is `HELLO` / `CAPABILITIES`. At minimum it reports:

- firmware version and Git revision;
- protocol major and minor;
- hardware revision when detectable;
- Video Out, IMU, GPIO capture, decoder, USB, and trace-buffer capabilities;
- safe maximum frame and stream sizes.

Clients may use a feature only when its capability is present. A protocol-major
mismatch is fatal for the session. A protocol-minor mismatch is allowed only
for capabilities understood by both sides.

## Resource Ownership

Only one session may own a mutable peripheral at a time. Session teardown must:

1. stop PIO state machines and DMA channels;
2. stop timers and producer callbacks;
3. return GPIO to a documented high-impedance state;
4. flush or discard bounded queues;
5. acknowledge completion before the client exits.

Watchdogs must recover from a disconnected Flipper without leaving GPIO driven
or USB host power enabled.

## Release Contract

Every published UF2 release must include:

- source tag and Git revision;
- firmware and protocol versions;
- supported hardware target;
- SHA-256 digest;
- release notes and breaking changes;
- stock-recovery instructions and verified recovery artifact reference.
