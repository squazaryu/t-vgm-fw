from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


class DashboardHostTests(unittest.TestCase):
    def test_dashboard_renderer_with_sanitizers(self):
        compiler = os.environ.get("CC") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler is unavailable")

        with tempfile.TemporaryDirectory() as directory_name:
            executable = Path(directory_name) / "dashboard_host_test"
            result = subprocess.run(
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
                    str(REPO_ROOT / "src" / "dashboard.c"),
                    str(REPO_ROOT / "tests" / "dashboard_host_test.c"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)

            environment = os.environ.copy()
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
            result = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                env=environment,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("dashboard_host_test: PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
