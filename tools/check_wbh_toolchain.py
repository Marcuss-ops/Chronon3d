#!/usr/bin/env python3
"""Validate the active machine/build against the checked-in WBH contract."""

from __future__ import annotations

import argparse
import hashlib
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tools/wbh_toolchain_manifest.env"
ALLOWED_KEYS = {
    "SCHEMA_VERSION", "PROFILE", "OS", "ARCH", "MIN_CMAKE", "MIN_NINJA",
    "MIN_GNU_CXX", "MIN_PYTHON", "REQUIRE_GLSLANG", "BUILD_TYPE",
    "GENERATOR", "COMPILER_FAMILY", "REQUIRED_CMAKE_OPTIONS",
}


def read_manifest(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{number}: expected KEY=VALUE")
        key, value = (part.strip() for part in line.split("=", 1))
        if key not in ALLOWED_KEYS:
            raise ValueError(f"{path}:{number}: unknown key {key}")
        if key in values or not value:
            raise ValueError(f"{path}:{number}: duplicate key or empty value: {key}")
        values[key] = value
    missing = ALLOWED_KEYS - values.keys()
    if missing:
        raise ValueError(f"{path}: missing keys: {', '.join(sorted(missing))}")
    if values["SCHEMA_VERSION"] != "1":
        raise ValueError("unsupported WBH manifest schema")
    return values


def run_version(command: list[str], pattern: str, label: str) -> tuple[tuple[int, ...], str]:
    executable = shutil.which(command[0])
    if not executable:
        raise RuntimeError(f"required executable missing: {command[0]}")
    result = subprocess.run([executable, *command[1:]], check=True,
                            capture_output=True, text=True)
    output = (result.stdout + result.stderr).strip()
    match = re.search(pattern, output)
    if not match:
        raise RuntimeError(f"cannot parse {label} version from: {output.splitlines()[0]}")
    return tuple(int(part) for part in match.groups()), output.splitlines()[0]


def version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def parse_cache(path: Path) -> dict[str, str]:
    cache: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if raw.startswith("//") or not raw or "=" not in raw:
            continue
        left, value = raw.split("=", 1)
        key = left.split(":", 1)[0]
        cache[key] = value
    return cache


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--build-dir", type=Path,
                        default=ROOT / "build/chronon/linux-video-test")
    parser.add_argument("--require-clean", action="store_true",
                        help="fail unless the exact HEAD tree is the tested worktree")
    args = parser.parse_args()
    try:
        contract = read_manifest(args.manifest)
        failures: list[str] = []
        checks: list[str] = []

        actual_os = platform.system()
        actual_arch = platform.machine()
        for label, actual, expected in (("OS", actual_os, contract["OS"]),
                                        ("architecture", actual_arch, contract["ARCH"])):
            (checks if actual == expected else failures).append(
                f"{label}: {actual} (required {expected})")

        for name, args_, pattern, minimum, label in (
            ("cmake", ["--version"], r"version\s+(\d+)\.(\d+)",
             contract["MIN_CMAKE"], "CMake"),
            ("ninja", ["--version"], r"^(\d+)\.(\d+)",
             contract["MIN_NINJA"], "Ninja"),
            ("c++", ["--version"], r"(?:Ubuntu\s+)?(\d+)\.(\d+)",
             contract["MIN_GNU_CXX"], "GNU C++"),
            ("python3", ["--version"], r"Python\s+(\d+)\.(\d+)",
             contract["MIN_PYTHON"], "Python"),
        ):
            version, banner = run_version([name, *args_], pattern, label)
            target = version_tuple(minimum)
            if label == "GNU C++" and not re.search(
                    r"g\+\+|gcc|gnu|ubuntu", banner, flags=re.IGNORECASE):
                failures.append(f"C++ compiler is not {contract['COMPILER_FAMILY']}: {banner}")
                continue
            (checks if version >= target else failures).append(
                f"{label}: {banner} (minimum {minimum})")

        if contract["REQUIRE_GLSLANG"] == "1":
            glslang = shutil.which("glslangValidator")
            if glslang:
                _, banner = run_version(["glslangValidator", "--version"],
                                        r"\b(\d+)\.(\d+)", "glslangValidator")
                checks.append(f"glslangValidator: {banner}")
            else:
                failures.append("required executable missing: glslangValidator")

        cache_path = args.build_dir / "CMakeCache.txt"
        if not cache_path.is_file():
            failures.append(f"configured build cache missing: {cache_path}")
        else:
            cache = parse_cache(cache_path)
            expected_cache = {
                "CMAKE_BUILD_TYPE": contract["BUILD_TYPE"],
                "CMAKE_GENERATOR": contract["GENERATOR"],
            }
            expected_cache.update({name: "ON" for name in
                                   contract["REQUIRED_CMAKE_OPTIONS"].split(",")})
            for key, expected in expected_cache.items():
                actual = cache.get(key, "<missing>")
                (checks if actual == expected else failures).append(
                    f"{key}: {actual} (required {expected})")

        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                              check=True, capture_output=True, text=True).stdout.strip()
        status = subprocess.run(["git", "status", "--porcelain=v1", "-z"], cwd=ROOT,
                                check=True, capture_output=True).stdout
        worktree_clean = not status
        state_hash = hashlib.sha256(status).hexdigest()[:16]
        if args.require_clean and not worktree_clean:
            failures.append("worktree is dirty; clean exact-SHA certification refused")
        print(f"WBH profile={contract['PROFILE']} sha={head} "
              f"worktree={'clean' if worktree_clean else 'dirty'} "
              f"status_sha256={state_hash}")
        for check in checks:
            print(f"PASS {check}")
        for failure in failures:
            print(f"FAIL {failure}", file=sys.stderr)
        print(f"WBH_TOOLCHAIN_{'PASS' if not failures else 'FAIL'}: "
              f"{len(checks)} checks, {len(failures)} failures")
        return 0 if not failures else 1
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"WBH_TOOLCHAIN_FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
