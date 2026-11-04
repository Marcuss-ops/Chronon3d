#!/usr/bin/env python3
"""Deterministic golden E2E gate for the Pareto production path.

Three small software renders exercise the capabilities that the 80/20 plan
treats as non-negotiable:

  1. editorial text      - shaped text over an image layer
  2. packshot 2.5D       - two depth-ordered image planes plus a caption
  3. mixed mesh          - inline mesh, depth-ordered plane and a caption

The gate renders every scene twice through the real CLI with `--video-sink
raw`, hashes each RGBA frame with SHA-256 and compares it with the committed
manifest. A single changed pixel, a missing frame, a size drift or a
non-deterministic render fails the gate.

Usage:
  python3 tests/e2e/golden/run_golden_e2e.py \
      --cli build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli

  # Regenerate the manifest after an intentional visual change.
  python3 tests/e2e/golden/run_golden_e2e.py --cli ... --update-goldens

The manifest is reviewed like code: an intentional visual change must update
the hashes in the same commit and carry its evidence. Never update goldens to
make an unexplained failure pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PLANS = HERE / "plans"
GOLDENS = HERE / "goldens.v1.json"
SCHEMA = "chronon3d.golden-e2e.v1"


def load_plan(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def frame_hashes(raw: Path, width: int, height: int, frames: int) -> list[str]:
    frame_bytes = width * height * 4
    expected = frame_bytes * frames
    size = raw.stat().st_size
    if size != expected:
        raise SystemExit(
            f"{raw}: raw sink is {size} bytes, expected {expected} "
            f"({width}x{height} RGBA8 x {frames} frames)"
        )
    data = raw.read_bytes()
    return [
        hashlib.sha256(data[index * frame_bytes : (index + 1) * frame_bytes]).hexdigest()
        for index in range(frames)
    ]


def render(cli: Path, plan_path: Path, out_path: Path) -> list[str]:
    plan = load_plan(plan_path)
    canvas = plan["canvas"]
    width = int(canvas["width"])
    height = int(canvas["height"])
    frames = int(canvas["duration_frames"])
    command = [
        str(cli),
        "render",
        "--plan",
        str(plan_path),
        "--assets-root",
        str(ROOT),
        "--backend",
        "software",
        "--start-frame",
        "0",
        "--end-frame",
        str(frames - 1),
        "--video-sink",
        "raw",
        "--output",
        str(out_path),
        "-v",
        "error",
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=900)
    if result.returncode != 0:
        print(f"FAIL {plan_path.name}: CLI exit {result.returncode}")
        print(result.stdout[-4000:])
        print(result.stderr[-4000:])
        raise SystemExit(1)
    return frame_hashes(out_path, width, height, frames)


def compare(name: str, actual: list[str], expected: dict) -> list[str]:
    failures: list[str] = []
    wanted = expected.get("sha256")
    if not isinstance(wanted, list):
        return [f"{name}: manifest has no sha256 list"]
    if len(actual) != len(wanted):
        failures.append(
            f"{name}: frame count {len(actual)} != manifest {len(wanted)}"
        )
    for index, (got, want) in enumerate(zip(actual, wanted)):
        if got != want:
            failures.append(
                f"{name}: frame {index} hash {got[:16]} != golden {want[:16]}"
            )
            if len(failures) >= 5:
                failures.append(f"{name}: further differences suppressed")
                break
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cli",
        default="build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli",
        help="chronon3d_cli executable",
    )
    parser.add_argument(
        "--update-goldens",
        action="store_true",
        help="rewrite goldens.v1.json after a reviewed visual change",
    )
    parser.add_argument("--only", default="", help="run only plans containing this text")
    parser.add_argument("--keep", action="store_true", help="keep rendered raw frames")
    args = parser.parse_args()

    cli = Path(args.cli)
    if not cli.is_absolute():
        cli = Path.cwd() / cli
    if not cli.is_file():
        raise SystemExit(f"CLI not found: {cli}")

    plans = sorted(PLANS.glob("*.plan.json"))
    if args.only:
        plans = [plan for plan in plans if args.only in plan.name]
    if not plans:
        raise SystemExit(f"no golden plans found in {PLANS}")

    manifest = None
    if GOLDENS.is_file() and not args.update_goldens:
        manifest = json.loads(GOLDENS.read_text(encoding="utf-8"))
        if manifest.get("schema") != SCHEMA:
            raise SystemExit(f"{GOLDENS}: unexpected schema {manifest.get('schema')}")

    results: dict[str, dict] = {}
    failures: list[str] = []
    total_frames = 0

    work_root = HERE / "out" if args.keep else None
    temp = None
    if work_root is None:
        temp = tempfile.TemporaryDirectory(prefix="chronon-golden-")
        work_root = Path(temp.name)
    work_root.mkdir(parents=True, exist_ok=True)

    for plan_path in plans:
        name = plan_path.name
        scene_dir = work_root / plan_path.stem
        scene_dir.mkdir(parents=True, exist_ok=True)
        first = scene_dir / "first.rgba"
        second = scene_dir / "second.rgba"
        plan = load_plan(plan_path)
        canvas = plan["canvas"]

        hashes_first = render(cli, plan_path, first)
        hashes_second = render(cli, plan_path, second)
        if hashes_first != hashes_second:
            failures.append(
                f"{name}: render is not deterministic "
                f"(two runs of the same plan differ)"
            )
        total_frames += len(hashes_first)
        results[name] = {
            "plan": plan_path.relative_to(HERE).as_posix(),
            "width": int(canvas["width"]),
            "height": int(canvas["height"]),
            "frames": int(canvas["duration_frames"]),
            "sha256": hashes_first,
        }

        if manifest is not None:
            expected = manifest.get("scenes", {}).get(name)
            if expected is None:
                failures.append(f"{name}: missing from {GOLDENS.name}")
            else:
                failures.extend(compare(name, hashes_first, expected))

        print(
            f"PASS {name}: {len(hashes_first)} frames, "
            f"{int(canvas['width'])}x{int(canvas['height'])}, deterministic"
        )

    if args.update_goldens:
        if failures:
            print("refusing to write goldens: the run failed", file=sys.stderr)
            for failure in failures:
                print("  " + failure, file=sys.stderr)
            return 1
        payload = {
            "schema": SCHEMA,
            "generator": "tests/e2e/golden/run_golden_e2e.py",
            "scenes": results,
        }
        GOLDENS.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote {GOLDENS} ({len(results)} scenes, {total_frames} frames)")
        return 0

    if failures:
        print("FAIL golden E2E:", file=sys.stderr)
        for failure in failures:
            print("  " + failure, file=sys.stderr)
        return 1

    print(f"all golden scenes match ({len(results)} scenes, {total_frames} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
