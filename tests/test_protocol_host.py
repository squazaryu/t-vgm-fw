from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


class ProtocolHostTests(unittest.TestCase):
    def test_c_codec_with_sanitizers(self):
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")

        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "protocol_host_test"
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-I",
                    str(REPO_ROOT / "include"),
                    str(REPO_ROOT / "src" / "protocol.c"),
                    str(REPO_ROOT / "tests" / "protocol_host_test.c"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stdout)

            environment = os.environ.copy()
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
            run_result = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                env=environment,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stdout)
            self.assertIn("protocol_host_test: PASS", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
