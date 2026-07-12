# t-vgm-fw

Experimental firmware for the RP2040 on the official Flipper Zero Video Game
Module. The project turns the module into a versioned coprocessor for Tumoflip
instead of treating it only as a video adapter.

This project is not affiliated with Flipper Devices. It is early-stage and does
not yet publish a flashable UF2.

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
See [docs/architecture.md](docs/architecture.md).

## Versioning

Development builds use:

```text
t-vgm-dev-<major>-<iteration>
```

The initial development line will start at `t-vgm-dev-001-001` after the Pico
SDK bootstrap is complete. Stable releases will use semantic versions and Git
tags such as `v1.0.0`.

## Status

The repository is in Phase 0: recovery baseline and hardware contract. Follow
the staged plan in [docs/roadmap.md](docs/roadmap.md).

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
