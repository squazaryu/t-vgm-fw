from __future__ import annotations

import hashlib
import json
import struct
import tempfile
import unittest
from dataclasses import asdict
from pathlib import Path

from tools.inspect_uf2 import (
    RP2040_FAMILY_ID,
    UF2_BLOCK_SIZE,
    UF2_FLAG_FAMILY_ID_PRESENT,
    UF2_MAGIC_END,
    UF2_MAGIC_START_0,
    UF2_MAGIC_START_1,
    Uf2ValidationError,
    inspect_uf2,
    validate_manifest,
)


def make_uf2(
    block_count: int = 2,
    *,
    payload_size: int = 256,
    gap_at: int | None = None,
    family_id: int = RP2040_FAMILY_ID,
) -> bytes:
    result = bytearray()
    address = 0x10000000
    for block_number in range(block_count):
        if gap_at == block_number:
            address += payload_size
        block = bytearray(UF2_BLOCK_SIZE)
        struct.pack_into(
            "<8I",
            block,
            0,
            UF2_MAGIC_START_0,
            UF2_MAGIC_START_1,
            UF2_FLAG_FAMILY_ID_PRESENT,
            address,
            payload_size,
            block_number,
            block_count,
            family_id,
        )
        block[32 : 32 + payload_size] = bytes([block_number & 0xFF]) * payload_size
        struct.pack_into("<I", block, UF2_BLOCK_SIZE - 4, UF2_MAGIC_END)
        result.extend(block)
        address += payload_size
    return bytes(result)


class InspectUf2Tests(unittest.TestCase):
    def inspect_bytes(self, data: bytes):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.uf2"
            path.write_bytes(data)
            return inspect_uf2(path)

    def test_valid_rp2040_image(self):
        data = make_uf2()
        info = self.inspect_bytes(data)

        self.assertEqual(info.sha256, hashlib.sha256(data).hexdigest())
        self.assertEqual(info.block_count, 2)
        self.assertEqual(info.payload_size, 256)
        self.assertEqual(info.family_id, "0xe48bff56")
        self.assertEqual(info.start_address, "0x10000000")
        self.assertEqual(info.end_address_exclusive, "0x10000200")
        self.assertTrue(info.contiguous)

    def test_rejects_bad_magic(self):
        data = bytearray(make_uf2())
        struct.pack_into("<I", data, 0, 0)
        with self.assertRaisesRegex(Uf2ValidationError, "start magic"):
            self.inspect_bytes(bytes(data))

    def test_rejects_non_256_byte_payload(self):
        with self.assertRaisesRegex(Uf2ValidationError, "exactly 256"):
            self.inspect_bytes(make_uf2(payload_size=128))

    def test_rejects_address_gap(self):
        with self.assertRaisesRegex(Uf2ValidationError, "gap or overlap"):
            self.inspect_bytes(make_uf2(gap_at=1))

    def test_rejects_wrong_family(self):
        with self.assertRaisesRegex(Uf2ValidationError, "expected RP2040"):
            self.inspect_bytes(make_uf2(family_id=0x12345678))

    def test_manifest_match_and_mismatch(self):
        info = self.inspect_bytes(make_uf2())
        values = asdict(info)
        manifest = {
            "schema_version": 1,
            "artifact": {
                "sha256": values["sha256"],
                "size_bytes": values["size_bytes"],
            },
            "uf2": {
                key: value
                for key, value in values.items()
                if key not in {"sha256", "size_bytes"}
            },
        }

        validate_manifest(info, json.loads(json.dumps(manifest)))
        manifest["artifact"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(Uf2ValidationError, "manifest mismatch"):
            validate_manifest(info, manifest)


if __name__ == "__main__":
    unittest.main()
