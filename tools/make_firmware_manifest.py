#!/usr/bin/env python3
"""Create deterministic, inspectable metadata for a TumoVGM firmware build."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any

try:
    from .inspect_uf2 import inspect_uf2
except ImportError:
    from inspect_uf2 import inspect_uf2

VERSION_PATTERN = re.compile(r"^t-vgm-(?:dev-\d{3}-\d{3}|\d+\.\d+\.\d+)$")
COMMIT_PATTERN = re.compile(r"^[0-9a-f]{12,40}$")
HARDWARE_TARGET = "flipper-video-game-module-rp2040"


def _artifact(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    return {
        "name": path.name,
        "sha256": hashlib.sha256(data).hexdigest(),
        "size_bytes": len(data),
    }


def build_manifest(
    *,
    elf: Path,
    binary: Path,
    uf2: Path,
    version: str,
    protocol_major: int,
    protocol_minor: int,
    git_commit: str,
    dirty: bool,
    pico_sdk_version: str,
    pico_sdk_commit: str,
    compiler_id: str,
    compiler_version: str,
) -> dict[str, Any]:
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"invalid firmware version: {version}")
    if not COMMIT_PATTERN.fullmatch(git_commit):
        raise ValueError(f"invalid firmware commit: {git_commit}")
    if not COMMIT_PATTERN.fullmatch(pico_sdk_commit):
        raise ValueError(f"invalid Pico SDK commit: {pico_sdk_commit}")
    if protocol_major < 0 or protocol_minor < 0:
        raise ValueError("protocol versions must be non-negative")

    uf2_info = inspect_uf2(uf2)
    uf2_artifact = _artifact(uf2)
    uf2_artifact["layout"] = {
        "block_count": uf2_info.block_count,
        "contiguous": uf2_info.contiguous,
        "end_address_exclusive": uf2_info.end_address_exclusive,
        "family_id": uf2_info.family_id,
        "flags": uf2_info.flags,
        "payload_size": uf2_info.payload_size,
        "start_address": uf2_info.start_address,
    }

    return {
        "artifacts": {
            "bin": _artifact(binary),
            "elf": _artifact(elf),
            "uf2": uf2_artifact,
        },
        "build": {
            "compiler_id": compiler_id,
            "compiler_version": compiler_version,
            "pico_sdk_commit": pico_sdk_commit,
            "pico_sdk_version": pico_sdk_version,
        },
        "firmware": {
            "dirty": dirty,
            "git_commit": git_commit,
            "hardware_target": HARDWARE_TARGET,
            "protocol": {
                "major": protocol_major,
                "minor": protocol_minor,
            },
            "version": version,
        },
        "schema_version": 1,
    }


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--bin", required=True, dest="binary", type=Path)
    parser.add_argument("--uf2", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--protocol-major", required=True, type=int)
    parser.add_argument("--protocol-minor", required=True, type=int)
    parser.add_argument("--git-commit", required=True)
    parser.add_argument("--dirty", required=True, choices=("0", "1"))
    parser.add_argument("--pico-sdk-version", required=True)
    parser.add_argument("--pico-sdk-commit", required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--compiler-version", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = build_manifest(
            elf=args.elf,
            binary=args.binary,
            uf2=args.uf2,
            version=args.version,
            protocol_major=args.protocol_major,
            protocol_minor=args.protocol_minor,
            git_commit=args.git_commit,
            dirty=args.dirty == "1",
            pico_sdk_version=args.pico_sdk_version,
            pico_sdk_commit=args.pico_sdk_commit,
            compiler_id=args.compiler_id,
            compiler_version=args.compiler_version,
        )
        write_manifest(args.output, manifest)
    except (OSError, ValueError) as error:
        raise SystemExit(f"error: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
