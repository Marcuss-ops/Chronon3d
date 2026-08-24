#!/usr/bin/env python3
"""Validate the stable files emitted by capture_crash_artifact.sh."""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def fail(message: str) -> int:
    print(f"CRASH_ARTIFACT_SCHEMA_FAIL: {message}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_dir", type=pathlib.Path)
    args = parser.parse_args()
    root = args.artifact_dir
    required = ["metadata.json", "result.json", "stdout.log", "stderr.log", "environment.txt"]
    missing = [name for name in required if not (root / name).is_file()]
    if missing:
        return fail("missing files: " + ", ".join(missing))

    try:
        metadata = json.loads((root / "metadata.json").read_text())
        result = json.loads((root / "result.json").read_text())
    except (OSError, json.JSONDecodeError) as error:
        return fail(f"invalid JSON: {error}")

    if metadata.get("schema_version") != 1:
        return fail("unsupported metadata schema_version")
    if not isinstance(metadata.get("command"), list) or not metadata["command"]:
        return fail("metadata.command must be a non-empty array")
    for key in ("timestamp_utc", "cwd", "platform"):
        if not isinstance(metadata.get(key), str) or not metadata[key]:
            return fail(f"metadata.{key} is missing")
    if not isinstance(result.get("exit_code"), int) or not isinstance(result.get("signal"), int):
        return fail("result must contain integer exit_code and signal")
    expected_signal = result["exit_code"] - 128 if result["exit_code"] >= 128 else 0
    if result["signal"] != expected_signal:
        return fail("result.signal does not match result.exit_code")

    print(f"CRASH_ARTIFACT_SCHEMA_PASS: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
