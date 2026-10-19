#!/usr/bin/env python3
"""Sample an imported SVG Trim Paths reveal at deterministic raw frames.

The existing H.264 gate checks its own authored endpoints. This gate extends
that evidence to f0/f15/f30/f45/f60 using the canonical software raw sink.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "svg_cli_gate"
PLAN = FIXTURE / "plan.json"
WIDTH = HEIGHT = 64
FRAME_BYTES = WIDTH * HEIGHT * 4
SAMPLES = (0, 15, 30, 45, 60)


def cli_path(override: str | None) -> Path:
    value = override or os.environ.get("CHRONON_CLI")
    if not value:
        raise SystemExit("set CHRONON_CLI or pass --cli to an SVG-enabled build")
    executable = Path(value).resolve()
    if not executable.is_file():
        raise SystemExit(f"chronon3d_cli not found: {executable}")
    return executable


def cyan_reveal_pixels(frame: bytes) -> int:
    count = 0
    for offset in range(0, len(frame), 4):
        pixel = offset // 4
        x, y = pixel % WIDTH, pixel // WIDTH
        red, green, blue, alpha = frame[offset:offset + 4]
        count += (28 <= x <= 60 and 28 <= y <= 40 and red < 100 and
                  green > 150 and blue > 150 and alpha > 128)
    return count


def render(executable: Path, work: Path, out: Path) -> bytes:
    plan = json.loads(PLAN.read_text())
    plan["canvas"]["duration_frames"] = 61
    trim = next(op for op in plan["layers"][1]["shape"]["operators"]
                if op["kind"] == "trim")
    trim["params"]["animation"]["keyframes"][-1]["frame"] = 45
    plan_path = work / "sampled_trim.plan.json"
    plan_path.write_text(json.dumps(plan))
    result = subprocess.run(
        [str(executable), "render", "--plan", str(plan_path),
         "--assets-root", str(FIXTURE.resolve()), "--backend", "software",
         "--video-sink", "raw", "--output", str(out)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    pixels = out.read_bytes()
    expected_bytes = FRAME_BYTES * 61
    if len(pixels) != expected_bytes:
        raise RuntimeError(f"raw sink produced {len(pixels)} bytes; expected {expected_bytes}")
    return pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    args = parser.parse_args()
    executable = cli_path(args.cli)

    with tempfile.TemporaryDirectory(prefix="svg_trim_sampled_") as temporary:
        work = Path(temporary)
        first = render(executable, work, work / "first.rgba")
        second = render(executable, work, work / "second.rgba")
    if first != second:
        raise RuntimeError("repeated raw Trim Paths renders differ pixel-for-pixel")

    counts = []
    for frame_number in SAMPLES:
        start = frame_number * FRAME_BYTES
        counts.append(cyan_reveal_pixels(first[start:start + FRAME_BYTES]))
    if counts[0] != 0 or not all(a <= b for a, b in zip(counts, counts[1:])):
        raise RuntimeError(f"Trim Paths pixel coverage did not grow monotonically: {counts}")
    if counts[2] <= counts[1] or counts[3] < 30 or counts[4] != counts[3]:
        raise RuntimeError(f"Trim Paths samples do not show draw-and-hold behavior: {counts}")
    print(f"SVG-02 imported Trim Paths raw pixels: PASS f0/15/30/45/60={counts}")
    print("SVG-08 repeat determinism: PASS (all 61 raw RGBA frames match)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG sampled Trim Paths gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
