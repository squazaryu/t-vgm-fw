from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.build_firmware import (
    BuildError,
    _prepare_build_dir,
    validate_manifest,
    validate_sdk,
    verify_reproducible,
)


class BuildFirmwareTests(unittest.TestCase):
    def test_missing_sdk_has_actionable_error(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(BuildError, "git submodule update"):
                validate_sdk(Path(directory) / "missing")

    def test_manifest_dirty_state_must_match_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(
                json.dumps({"schema_version": 1, "firmware": {"dirty": True}}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BuildError, "dirty state"):
                validate_manifest(path, dirty=False)

    def test_manifest_schema_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps({"schema_version": 2}), encoding="utf-8")
            with self.assertRaisesRegex(BuildError, "schema"):
                validate_manifest(path, dirty=False)

    def test_empty_existing_build_directory_is_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            _prepare_build_dir(path, clean=False)
            self.assertTrue(path.is_dir())

    def test_nonempty_build_directory_requires_clean(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "stale").write_text("stale", encoding="utf-8")
            with self.assertRaisesRegex(BuildError, "--clean"):
                _prepare_build_dir(path, clean=False)

    def test_reproducibility_checks_all_publishable_binary_artifacts(self):
        with tempfile.TemporaryDirectory() as first_name:
            with tempfile.TemporaryDirectory() as second_name:
                first_dir = Path(first_name)
                second_dir = Path(second_name)
                first = {}
                second = {}
                for name in ("bin", "elf", "manifest", "uf2"):
                    first[name] = first_dir / name
                    second[name] = second_dir / name
                    first[name].write_bytes(name.encode())
                    second[name].write_bytes(name.encode())

                verify_reproducible(first, second)
                second["elf"].write_bytes(b"changed")
                with self.assertRaisesRegex(BuildError, "ELF files differ"):
                    verify_reproducible(first, second)


if __name__ == "__main__":
    unittest.main()
