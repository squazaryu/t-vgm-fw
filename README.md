# t-vgm-fw

Experimental firmware for the RP2040 on the official Flipper Zero Video Game
Module. The project turns the module into a versioned coprocessor for Tumoflip
instead of treating it only as a video adapter.

This project is not affiliated with Flipper Devices. It is early-stage and does
not yet publish a stable UF2 release.

## Intended Capabilities

- External Video Out dashboards and diagnostics.
- Versioned communication with a Tumoflip FAP client.
- ICM-42688-P IMU streaming and gesture events.
- `TumoScope V2` GPIO capture using RP2040 PIO and DMA.
- Hardware triggering and bounded protocol decoding.
- Optional standalone trace buffering and USB experiments.

## Safety Boundary

No feature work may require flashing a development UF2 until the recovery gate
in issue `#1` is complete. The gate must prove that the exact module can enter
RP2040 BOOT mode, accept a test UF2, and return to a verified stock image.

The first firmware must be observational only. It must not drive external GPIO,
enable USB host power, or transmit captured data outside an explicit session.

## Architecture

The system is split into independently versioned components:

- `t-vgm-fw`: RP2040 firmware and UF2 release artifacts.
- `tumoflip`: the Flipper Zero FAP client and device integration.
- `unleashed-companion`: optional package discovery, installation, and
  diagnostics on iOS.

The wire contract uses a versioned, framed protocol with capability negotiation.
Firmware and clients must fail closed when their protocol major versions differ.
See [docs/protocol-v1.md](docs/protocol-v1.md) for the normative v1 contract and
[docs/architecture.md](docs/architecture.md) for the system boundaries.

## Versioning

Development builds use:

```text
t-vgm-dev-<major>-<iteration>
```

The recovery probe used `t-vgm-dev-001-001`. The reproducible build foundation
advanced the line to `t-vgm-dev-001-002`. Protocol v1.0 advanced it to
`t-vgm-dev-001-003`; the first UART bridge endpoint advanced it to
`t-vgm-dev-001-004` with protocol v1.1. The Video Out diagnostics renderer uses
`t-vgm-dev-001-005` while keeping protocol v1.1. Bounded ICM-42688-P telemetry
advances the line to `t-vgm-dev-001-006` and protocol v1.2. Each accepted
issue-level firmware change advances the final three-digit iteration. Stable
releases will use semantic versions and Git tags such as `v1.0.0`.

## Building

The build is pinned to Pico SDK `1.5.1` at commit `6a7db34f` and GNU Arm
Embedded GCC `12.3.1`. Initialize only the submodules required by the RP2040
target:

```sh
git submodule update --init third_party/pico-sdk
git -C third_party/pico-sdk submodule update --init lib/tinyusb
git submodule update --init third_party/pico-dvi
```

Install CMake and Ninja. On macOS, the GCC toolchain already bundled with a
Tumoflip checkout can be selected explicitly:

```sh
brew install cmake ninja
export ARM_TOOLCHAIN_BIN=/path/to/tumoflip/toolchain/arm64-darwin/bin
```

Build from a clean source tree and prove that a second independent build
produces the same UF2 bytes:

```sh
python3 tools/build_firmware.py --clean --verify-reproducible
```

The build produces:

- `build/tumovgm_firmware.elf`
- `build/tumovgm_firmware.elf.map`
- `build/tumovgm_firmware.bin`
- `build/tumovgm_firmware.uf2`
- `build/tumovgm_firmware.manifest.json`

The manifest exposes firmware, protocol, Git, SDK, compiler, target, size, and
SHA-256 information without flashing the module. The normal build refuses a
dirty source tree. `--allow-dirty` is available only for local development and
must not be used for hardware acceptance or published artifacts.

## Status

Phase 0 and the native Flipper bridge client are accepted on physical hardware.
The current Phase 1 development line adds a deterministic 640x480 Video Out
diagnostics dashboard while keeping UART protocol processing isolated on the
other RP2040 core. Follow the staged plan in [docs/roadmap.md](docs/roadmap.md).

The official stock `vgm-fw-0.1.0.uf2` recovery image is recorded by a
commit-pinned manifest rather than redistributed here. Validate a downloaded
copy before any hardware experiment:

```sh
python3 tools/inspect_uf2.py \
  --manifest recovery/stock-vgm-fw-0.1.0.json \
  /path/to/vgm-fw-0.1.0.uf2
```

See [docs/recovery.md](docs/recovery.md) before flashing any development UF2.

## Official References

- [Video Game Module overview](https://docs.flipper.net/zero/video-game-module)
- [GPIO and pinout](https://docs.flipper.net/zero/video-game-module/gpio)
- [Custom firmware installation](https://docs.flipper.net/zero/video-game-module/custom-firmware)

## License

GPL-3.0. See [LICENSE](LICENSE).
