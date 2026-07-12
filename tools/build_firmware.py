#!/usr/bin/env python3
"""Build TumoVGM with pinned dependencies and optionally prove reproducibility."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SDK = REPO_ROOT / "third_party" / "pico-sdk"
EXPECTED_SDK_COMMIT = "6a7db34ff63345a7badec79ebea3aaef1712f374"
EXPECTED_TINYUSB_COMMIT = "86c416d4c0fb38432460b3e11b08b9de76941bf5"
EXPECTED_GCC_VERSION = "12.3.1"
OUTPUT_BASE = "tumovgm_firmware"


class BuildError(RuntimeError):
    pass


def _run(command: list[str], *, cwd: Path = REPO_ROOT, env: dict[str, str] | None = None) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except FileNotFoundError as error:
        raise BuildError(f"required command not found: {command[0]}") from error
    except subprocess.CalledProcessError as error:
        raise BuildError(
            f"command failed ({error.returncode}): {' '.join(command)}\n{error.stdout}"
        ) from error
    return result.stdout.strip()


def _git_head(path: Path) -> str:
    return _run(["git", "rev-parse", "HEAD"], cwd=path)


def validate_sdk(path: Path) -> None:
    if not (path / "pico_sdk_init.cmake").is_file():
        raise BuildError(
            "Pico SDK is missing. Run: git submodule update --init third_party/pico-sdk"
        )
    if _git_head(path) != EXPECTED_SDK_COMMIT:
        raise BuildError(
            f"unsupported Pico SDK commit; expected {EXPECTED_SDK_COMMIT}"
        )

    tinyusb = path / "lib" / "tinyusb"
    if not (tinyusb / "src" / "tusb.c").is_file():
        raise BuildError(
            "TinyUSB is missing. Run: "
            "git -C third_party/pico-sdk submodule update --init lib/tinyusb"
        )
    if _git_head(tinyusb) != EXPECTED_TINYUSB_COMMIT:
        raise BuildError(
            f"unsupported TinyUSB commit; expected {EXPECTED_TINYUSB_COMMIT}"
        )


def source_is_dirty() -> bool:
    return bool(_run(["git", "status", "--porcelain", "--untracked-files=normal"]))


def tool_environment(toolchain_bin: Path | None) -> tuple[dict[str, str], Path]:
    env = os.environ.copy()
    if toolchain_bin:
        toolchain_bin = toolchain_bin.resolve()
        env["PATH"] = f"{toolchain_bin}{os.pathsep}{env.get('PATH', '')}"

    compiler = shutil.which("arm-none-eabi-gcc", path=env.get("PATH"))
    if not compiler:
        raise BuildError(
            "arm-none-eabi-gcc is missing; pass --toolchain-bin or set ARM_TOOLCHAIN_BIN"
        )
    version = _run([compiler, "-dumpfullversion"], env=env)
    if version != EXPECTED_GCC_VERSION:
        raise BuildError(
            f"unsupported arm-none-eabi-gcc {version}; expected {EXPECTED_GCC_VERSION}"
        )
    return env, Path(compiler)


def _require_host_tools(env: dict[str, str]) -> tuple[str, str]:
    cmake = shutil.which("cmake", path=env.get("PATH"))
    ninja = shutil.which("ninja", path=env.get("PATH"))
    if not cmake:
        raise BuildError("cmake is missing")
    if not ninja:
        raise BuildError("ninja is missing")
    return cmake, ninja


def _prepare_build_dir(path: Path, *, clean: bool) -> None:
    path = path.resolve()
    if path == REPO_ROOT or path == Path(path.anchor):
        raise BuildError(f"unsafe build directory: {path}")
    if path.exists():
        if not any(path.iterdir()):
            return
        if not clean:
            raise BuildError(f"build directory exists; pass --clean to replace it: {path}")
        shutil.rmtree(path)
    path.mkdir(parents=True)


def build_once(
    build_dir: Path,
    *,
    sdk: Path,
    env: dict[str, str],
    cmake: str,
    clean: bool,
) -> dict[str, Path]:
    _prepare_build_dir(build_dir, clean=clean)
    _run(
        [
            cmake,
            "-S",
            str(REPO_ROOT),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DPICO_BOARD=pico",
            f"-DPICO_SDK_PATH={sdk}",
        ],
        env=env,
    )
    _run([cmake, "--build", str(build_dir)], env=env)

    artifacts = {
        "bin": build_dir / f"{OUTPUT_BASE}.bin",
        "elf": build_dir / f"{OUTPUT_BASE}.elf",
        "map": build_dir / f"{OUTPUT_BASE}.elf.map",
        "manifest": build_dir / f"{OUTPUT_BASE}.manifest.json",
        "uf2": build_dir / f"{OUTPUT_BASE}.uf2",
    }
    missing = [str(path) for path in artifacts.values() if not path.is_file()]
    if missing:
        raise BuildError("missing build artifacts: " + ", ".join(missing))
    return artifacts


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_reproducible(first: dict[str, Path], second: dict[str, Path]) -> None:
    for artifact in ("bin", "elf", "manifest", "uf2"):
        if first[artifact].read_bytes() != second[artifact].read_bytes():
            raise BuildError(
                f"reproducibility check failed: {artifact.upper()} files differ"
            )


def validate_manifest(path: Path, *, dirty: bool) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BuildError(f"invalid firmware manifest: {error}") from error
    if manifest.get("schema_version") != 1:
        raise BuildError("unexpected firmware manifest schema")
    firmware = manifest.get("firmware", {})
    if firmware.get("dirty") is not dirty:
        raise BuildError("firmware manifest dirty state does not match source tree")
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--sdk", type=Path, default=DEFAULT_SDK)
    parser.add_argument(
        "--toolchain-bin",
        type=Path,
        default=Path(os.environ["ARM_TOOLCHAIN_BIN"])
        if os.environ.get("ARM_TOOLCHAIN_BIN")
        else None,
    )
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--verify-reproducible", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        sdk = args.sdk.resolve()
        validate_sdk(sdk)
        dirty = source_is_dirty()
        if dirty and not args.allow_dirty:
            raise BuildError("source tree is dirty; commit changes or pass --allow-dirty")

        env, compiler = tool_environment(args.toolchain_bin)
        cmake, _ = _require_host_tools(env)
        artifacts = build_once(
            args.build_dir.resolve(), sdk=sdk, env=env, cmake=cmake, clean=args.clean
        )
        manifest = validate_manifest(artifacts["manifest"], dirty=dirty)

        if args.verify_reproducible:
            with tempfile.TemporaryDirectory(prefix="t-vgm-repro-") as directory:
                second = build_once(
                    Path(directory), sdk=sdk, env=env, cmake=cmake, clean=False
                )
                verify_reproducible(artifacts, second)

        summary = {
            "compiler": str(compiler),
            "dirty": dirty,
            "firmware": manifest["firmware"],
            "manifest": str(artifacts["manifest"]),
            "reproducible": args.verify_reproducible,
            "uf2": str(artifacts["uf2"]),
            "uf2_sha256": _sha256(artifacts["uf2"]),
        }
        print(json.dumps(summary, indent=2, sort_keys=True))
    except BuildError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
