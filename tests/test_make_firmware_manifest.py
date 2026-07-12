from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path

from tools.inspect_uf2 import (
    RP2040_FAMILY_ID,
    UF2_BLOCK_SIZE,
    UF2_FLAG_FAMILY_ID_PRESENT,
    UF2_MAGIC_END,
    UF2_MAGIC_START_0,
    UF2_MAGIC_START_1,
)
from tools.make_firmware_manifest import build_manifest, write_manifest


def make_uf2() -> bytes:
    block = bytearray(UF2_BLOCK_SIZE)
    struct.pack_into(
        "<8I",
        block,
        0,
        UF2_MAGIC_START_0,
        UF2_MAGIC_START_1,
        UF2_FLAG_FAMILY_ID_PRESENT,
        0x10000000,
        256,
        0,
        1,
        RP2040_FAMILY_ID,
    )
    block[32:288] = bytes(range(256))
    struct.pack_into("<I", block, UF2_BLOCK_SIZE - 4, UF2_MAGIC_END)
    return bytes(block)


class FirmwareManifestTests(unittest.TestCase):
    def create_manifest(self, directory: Path):
        elf = directory / "firmware.elf"
        binary = directory / "firmware.bin"
        uf2 = directory / "firmware.uf2"
        elf.write_bytes(b"ELF fixture")
        binary.write_bytes(b"BIN fixture")
        uf2.write_bytes(make_uf2())
        return build_manifest(
            elf=elf,
            binary=binary,
            uf2=uf2,
            version="t-vgm-dev-001-001",
            protocol_major=0,
            protocol_minor=0,
            git_commit="0123456789ab",
            dirty=False,
            pico_sdk_version="1.5.1",
            pico_sdk_commit="6a7db34ff63345a7badec79ebea3aaef1712f374",
            compiler_id="GNU",
            compiler_version="12.3.1",
        )

    def test_manifest_contains_inspectable_identity_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory_name:
            manifest = self.create_manifest(Path(directory_name))

        self.assertEqual(manifest["schema_version"], 1)
        self.assertEqual(manifest["firmware"]["version"], "t-vgm-dev-001-001")
        self.assertEqual(manifest["firmware"]["protocol"], {"major": 0, "minor": 0})
        self.assertFalse(manifest["firmware"]["dirty"])
        self.assertEqual(manifest["build"]["pico_sdk_version"], "1.5.1")
        self.assertEqual(manifest["build"]["compiler_version"], "12.3.1")
        self.assertEqual(manifest["artifacts"]["uf2"]["layout"]["family_id"], "0xe48bff56")

    def test_manifest_serialization_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            manifest = self.create_manifest(directory)
            first = directory / "first.json"
            second = directory / "second.json"
            write_manifest(first, manifest)
            write_manifest(second, json.loads(json.dumps(manifest)))
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_rejects_invalid_version(self):
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            elf = directory / "firmware.elf"
            binary = directory / "firmware.bin"
            uf2 = directory / "firmware.uf2"
            elf.write_bytes(b"elf")
            binary.write_bytes(b"bin")
            uf2.write_bytes(make_uf2())
            with self.assertRaisesRegex(ValueError, "invalid firmware version"):
                build_manifest(
                    elf=elf,
                    binary=binary,
                    uf2=uf2,
                    version="dirty-local",
                    protocol_major=0,
                    protocol_minor=0,
                    git_commit="0123456789ab",
                    dirty=True,
                    pico_sdk_version="1.5.1",
                    pico_sdk_commit="6a7db34ff63345a7badec79ebea3aaef1712f374",
                    compiler_id="GNU",
                    compiler_version="12.3.1",
                )


if __name__ == "__main__":
    unittest.main()
