from __future__ import annotations

import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

from tools.generate_protocol_constants import render

REPO_ROOT = Path(__file__).resolve().parents[1]


class ProtocolSchemaTests(unittest.TestCase):
    def test_generated_header_matches_schema(self):
        result = subprocess.run(
            [sys.executable, "tools/generate_protocol_constants.py", "--check"],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_schema_wire_limits_are_stable(self):
        schema = json.loads(
            (REPO_ROOT / "protocol" / "protocol-v1.json").read_text(encoding="utf-8")
        )
        self.assertEqual(schema["schema_version"], 1)
        self.assertEqual(schema["protocol"]["magic_ascii"], "TVG1")
        self.assertEqual(schema["protocol"]["major"], 1)
        self.assertEqual(schema["protocol"]["minor"], 0)
        self.assertEqual(schema["protocol"]["max_payload"], 512)
        self.assertEqual(len(set(schema["messages"].values())), len(schema["messages"]))

        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        major = re.search(r"set\(TUMOVGM_PROTOCOL_MAJOR (\d+)\)", cmake)
        minor = re.search(r"set\(TUMOVGM_PROTOCOL_MINOR (\d+)\)", cmake)
        self.assertIsNotNone(major)
        self.assertIsNotNone(minor)
        self.assertEqual(int(major.group(1)), schema["protocol"]["major"])
        self.assertEqual(int(minor.group(1)), schema["protocol"]["minor"])

    def test_generator_rejects_duplicate_wire_ids(self):
        schema = json.loads(
            (REPO_ROOT / "protocol" / "protocol-v1.json").read_text(encoding="utf-8")
        )
        schema["messages"]["PING"] = schema["messages"]["HELLO"]
        with self.assertRaisesRegex(ValueError, "messages values must be unique"):
            render(schema)


if __name__ == "__main__":
    unittest.main()
