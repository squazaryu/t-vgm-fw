# Stock Recovery Baseline

This procedure is the hardware gate for custom TumoVGM firmware. Do not install
a development UF2 until the official recovery artifact passes local validation.

## Official Recovery Artifact

The Video Game Module Tool maintained by Flipper Devices bundles the stock
firmware as `vgm-fw-0.1.0.uf2`.

- Repository: <https://github.com/flipperdevices/flipperzero-good-faps>
- Pinned commit: `126ecf63a570a51a49ad89991cd77ffa54c1de2c`
- Pinned file: <https://raw.githubusercontent.com/flipperdevices/flipperzero-good-faps/126ecf63a570a51a49ad89991cd77ffa54c1de2c/video_game_module_tool/files/vgm-fw-0.1.0.uf2>
- SHA-256: `008c7ede9f99dd4cad1cbe0260fed4792d5654e7c3c731218be69fa94ced84e8`
- Size: `303616` bytes

The repository does not redistribute this binary. The pinned metadata lives in
[`recovery/stock-vgm-fw-0.1.0.json`](../recovery/stock-vgm-fw-0.1.0.json).

## Validate Before Use

Download the pinned file and validate both its digest and UF2 structure:

```sh
curl --fail --location \
  --output /tmp/vgm-fw-0.1.0.uf2 \
  https://raw.githubusercontent.com/flipperdevices/flipperzero-good-faps/126ecf63a570a51a49ad89991cd77ffa54c1de2c/video_game_module_tool/files/vgm-fw-0.1.0.uf2

python3 tools/inspect_uf2.py \
  --manifest recovery/stock-vgm-fw-0.1.0.json \
  /tmp/vgm-fw-0.1.0.uf2
```

The validator enforces the restrictions documented for the Flipper Video Game
Module Tool: 256-byte payloads, 256-byte aligned contiguous target addresses,
main-flash blocks only, and no file-container or extension-tag blocks.

## Enter RP2040 BOOT Mode

1. Disconnect the module from Flipper Zero and USB-C.
2. Hold the recessed BOOT button on the module.
3. Connect the module directly to macOS over USB-C.
4. Release BOOT only after the `RPI-RP2` volume appears.
5. Confirm that no other RP2040 mass-storage device is connected.

Entering BOOT mode does not erase the installed firmware. Do not copy a UF2 as
part of the first detection check.

## Restore Stock Firmware

With the validated stock image and `RPI-RP2` mounted:

```sh
cp /tmp/vgm-fw-0.1.0.uf2 /Volumes/RPI-RP2/
```

The volume should unmount automatically when the RP2040 reboots. Reconnect the
module to Flipper Zero and verify the normal Video Game Module application,
Video Out, and IMU before marking recovery successful.

The same validated UF2 can be installed from the Flipper Video Game Module Tool.
That route must be tested separately because its UF2 parser has stricter input
requirements than the RP2040 USB bootloader.

## Development Probe Rules

The first TumoVGM probe is `t-vgm-dev-001-001`. It must:

- configure every exposed GPIO as input with pulls disabled;
- keep USB host power disabled;
- avoid flash writes after boot;
- expose only firmware identity and a heartbeat;
- support recovery through the physical BOOT button;
- be followed immediately by a verified stock restore.

Build it against the same Pico SDK revision used by the official VGM baseline:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk-1.5.1
export PATH=/path/to/arm-none-eabi/bin:$PATH

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=pico
cmake --build build

python3 tools/inspect_uf2.py build/tumovgm_recovery_probe.uf2
```

The build reports `TumoVGM dirty: 0` only when tracked and untracked source
changes are committed. Only a clean build may be installed on hardware.

## Source Boundary

The official `flipperdevices/video-game-module` repository is useful as a
hardware and behavior reference but currently has no explicit repository
license. TumoVGM implementation must therefore be written independently using
licensed Pico SDK components and public hardware documentation. Do not copy
official source files into this repository unless Flipper Devices publishes
compatible license terms.
