#!/usr/bin/env python3
"""Validate a VGM-compatible RP2040 UF2 image and print normalized metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

UF2_BLOCK_SIZE = 512
UF2_HEADER_SIZE = 32
UF2_MAGIC_START_0 = 0x0A324655
UF2_MAGIC_START_1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_NOT_MAIN_FLASH = 0x00000001
UF2_FLAG_FILE_CONTAINER = 0x00001000
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
UF2_FLAG_EXTENSION_TAGS = 0x00008000
RP2040_FAMILY_ID = 0xE48BFF56
VGM_PAYLOAD_SIZE = 256


class Uf2ValidationError(ValueError):
    """Raised when an image cannot be safely handled as a VGM firmware UF2."""


@dataclass(frozen=True)
class Uf2Info:
    sha256: str
    size_bytes: int
    block_count: int
    payload_size: int
    flags: str
    family_id: str
    start_address: str
    end_address_exclusive: str
    contiguous: bool


def _hex32(value: int) -> str:
    return f"0x{value:08x}"


def inspect_uf2(path: Path) -> Uf2Info:
    data = path.read_bytes()
    if not data:
        raise Uf2ValidationError("UF2 file is empty")
    if len(data) % UF2_BLOCK_SIZE:
        raise Uf2ValidationError("UF2 size is not a multiple of 512 bytes")

    blocks: list[tuple[int, int, int, int, int, int]] = []
    for index, offset in enumerate(range(0, len(data), UF2_BLOCK_SIZE)):
        block = data[offset : offset + UF2_BLOCK_SIZE]
        (
            magic_start_0,
            magic_start_1,
            flags,
            target_address,
            payload_size,
            block_number,
            declared_block_count,
            family_id,
        ) = struct.unpack_from("<8I", block)
        (magic_end,) = struct.unpack_from("<I", block, UF2_BLOCK_SIZE - 4)

        if magic_start_0 != UF2_MAGIC_START_0 or magic_start_1 != UF2_MAGIC_START_1:
            raise Uf2ValidationError(f"block {index}: invalid UF2 start magic")
        if magic_end != UF2_MAGIC_END:
            raise Uf2ValidationError(f"block {index}: invalid UF2 end magic")
        if payload_size != VGM_PAYLOAD_SIZE:
            raise Uf2ValidationError(
                f"block {index}: payload must be exactly {VGM_PAYLOAD_SIZE} bytes"
            )
        if target_address % VGM_PAYLOAD_SIZE:
            raise Uf2ValidationError(f"block {index}: target address is not 256-byte aligned")
        if block_number != index:
            raise Uf2ValidationError(f"block {index}: unexpected block number {block_number}")
        if flags & UF2_FLAG_NOT_MAIN_FLASH:
            raise Uf2ValidationError(f"block {index}: non-flash block is not supported")
        if flags & UF2_FLAG_FILE_CONTAINER:
            raise Uf2ValidationError(f"block {index}: file-container block is not supported")
        if flags & UF2_FLAG_EXTENSION_TAGS:
            raise Uf2ValidationError(f"block {index}: extension tags are not supported")
        if not flags & UF2_FLAG_FAMILY_ID_PRESENT:
            raise Uf2ValidationError(f"block {index}: RP2040 family ID is missing")
        if family_id != RP2040_FAMILY_ID:
            raise Uf2ValidationError(
                f"block {index}: expected RP2040 family {_hex32(RP2040_FAMILY_ID)}, "
                f"got {_hex32(family_id)}"
            )

        blocks.append(
            (flags, target_address, payload_size, block_number, declared_block_count, family_id)
        )

    expected_count = len(blocks)
    for index, block in enumerate(blocks):
        if block[4] != expected_count:
            raise Uf2ValidationError(
                f"block {index}: declares {block[4]} blocks, file contains {expected_count}"
            )

    first_flags, start_address, payload_size, _, _, family_id = blocks[0]
    previous_end = start_address
    for index, block in enumerate(blocks):
        flags, target_address, current_payload_size, _, _, current_family_id = block
        if flags != first_flags:
            raise Uf2ValidationError(f"block {index}: inconsistent flags")
        if current_family_id != family_id:
            raise Uf2ValidationError(f"block {index}: inconsistent family ID")
        if target_address != previous_end:
            raise Uf2ValidationError(
                f"block {index}: address gap or overlap at {_hex32(target_address)}"
            )
        previous_end = target_address + current_payload_size

    return Uf2Info(
        sha256=hashlib.sha256(data).hexdigest(),
        size_bytes=len(data),
        block_count=expected_count,
        payload_size=payload_size,
        flags=_hex32(first_flags),
        family_id=_hex32(family_id),
        start_address=_hex32(start_address),
        end_address_exclusive=_hex32(previous_end),
        contiguous=True,
    )


def validate_manifest(info: Uf2Info, manifest: dict[str, Any]) -> None:
    if manifest.get("schema_version") != 1:
        raise Uf2ValidationError("unsupported recovery manifest schema")

    artifact = manifest.get("artifact")
    expected_uf2 = manifest.get("uf2")
    if not isinstance(artifact, dict) or not isinstance(expected_uf2, dict):
        raise Uf2ValidationError("manifest must contain artifact and uf2 objects")

    actual = asdict(info)
    expected = {
        "sha256": artifact.get("sha256"),
        "size_bytes": artifact.get("size_bytes"),
        "block_count": expected_uf2.get("block_count"),
        "payload_size": expected_uf2.get("payload_size"),
        "flags": expected_uf2.get("flags"),
        "family_id": expected_uf2.get("family_id"),
        "start_address": expected_uf2.get("start_address"),
        "end_address_exclusive": expected_uf2.get("end_address_exclusive"),
        "contiguous": expected_uf2.get("contiguous"),
    }
    mismatches = [
        f"{key}: expected {expected[key]!r}, got {actual[key]!r}"
        for key in expected
        if actual[key] != expected[key]
    ]
    if mismatches:
        raise Uf2ValidationError("manifest mismatch: " + "; ".join(mismatches))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uf2", type=Path, help="UF2 image to inspect")
    parser.add_argument("--manifest", type=Path, help="optional recovery manifest to enforce")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        info = inspect_uf2(args.uf2)
        if args.manifest:
            with args.manifest.open("r", encoding="utf-8") as manifest_file:
                validate_manifest(info, json.load(manifest_file))
    except (OSError, json.JSONDecodeError, Uf2ValidationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(json.dumps(asdict(info), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
