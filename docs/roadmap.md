# Roadmap

## Phase 0: Recovery and Hardware Baseline

- Verify BOOT-mode entry and exact RP2040 identity.
- Locate and checksum the stock recovery artifact.
- Flash a minimal non-driving test UF2 and restore stock firmware.
- Record the Video Game Module pin and peripheral reservations.

No later phase may flash hardware until this gate is accepted on the connected
module.

## Phase 1: TumoVGM Bridge

- Add the reproducible Pico SDK build and CI pipeline.
- Define the framed protocol and capability negotiation.
- Build the Tumoflip bridge client.
- Render a branded diagnostic frame over Video Out.
- Stream IMU samples with bounded rates and clean session teardown.

Acceptance requires repeated connect, disconnect, reboot, and stock-recovery
tests without leaving the Flipper or module unavailable.

## Phase 2: TumoScope V2

- Capture safe 3.3 V digital GPIO through PIO and DMA.
- Add hardware edge and pattern triggers.
- Display a live waveform on Video Out and a compact view on Flipper.
- Save deterministic trace files through the Flipper client.
- Add bounded UART, SPI, and I2C decoding.

The capture engine must never drive an input pin and must reject unsupported
sample-rate and buffer combinations before starting DMA.

## Phase 3: Coprocessor Platform

- GPIO Workbench and safe signal generation.
- Standalone trace buffering and session recovery.
- USB device/host laboratory with explicit power controls.
- TumoModule Runtime target and TumoFabric node.
- Companion-side UF2 discovery, compatibility checks, and install reporting.

These are independent feature tracks. Each requires its own threat model or
hardware safety review before implementation.
