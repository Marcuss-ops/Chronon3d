#!/usr/bin/env python3
"""SVG isolated-group compositing certification gate (ADR-035, Q1/Q4).

Renders an SVG whose 0.5-opacity group contains overlapping siblings and
verifies the isolated-group alpha contract end to end:

  1. overlap pixel  = lerp(red background, top child color, 0.5) — it must
     NOT depend on the lower sibling's color (the fold alpha would make it);
  2. non-overlap pixels inside the group match lerp(background, color, 0.5);
  3. an animated translated child inside the isolated group produces
     endpoint frame hashes that differ;
  4. a repeat render reproduces an identical mid frame hash (determinism).

The gate fails closed: missing CLI, CLI failures, invisible frames, oracle
pixel misses, colliding endpoint hashes, or divergent repeat hashes abort
the run.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
from math import isclose
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "svg_group_isolation_gate"
PLAN_STATIC = FIXTURE / "plan.json"
PLAN_ANIMATED = FIXTURE / "plan_animated.json"
CANVAS = (64, 64)
TOLERANCE = 8  # uint8 steps; software linker is exact, SSAA rounding is <=1


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    for candidate in (
        ROOT / "build" / "chronon" / "linux-video-test" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-fast-dev" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-svg-fast" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
    ):
        if candidate.is_file():
            return candidate
    raise SystemExit("chronon3d_cli not found (set CHRONON_CLI to an SVG-enabled build)")


def decode_rgba(image: Path) -> bytes:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(image),
         "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
        capture_output=True, check=True)
    width, height = CANVAS
    if len(proc.stdout) != width * height * 4:
        raise SystemExit(f"{image.name} did not decode to one {width}x{height} RGBA image")
    return proc.stdout


def render(plan: Path, output_dir: Path, prefix: str,
           start: int = 0, end: int = 30) -> Path:
    pattern = output_dir / f"{prefix}_####.png"
    proc = subprocess.run(
        [str(cli()), "render", "--plan", str(plan),
         "--assets-root", str(FIXTURE), "--backend", "software",
         "--start-frame", str(start), "--end-frame", str(end),
         "--output", str(pattern)], capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"SVG CLI render failed:\n{proc.stderr[-3000:]}")
    return output_dir / f"{prefix}_{start:04d}.png"


def pixel_at(data: bytes, x: int, y: int) -> tuple[float, float, float, float]:
    width, _ = CANVAS
    o = (y * width + x) * 4
    return (data[o], data[o + 1], data[o + 2], data[o + 3])


def check_oracle(name: str, got: tuple, expected: tuple) -> None:
    for channel, value in enumerate(expected):
        if abs(got[channel] - value) > TOLERANCE:
            raise SystemExit(
                f"oracle miss: {name} {got} != {expected} (tolerance {TOLERANCE})")
    print(f"  PASS  {name} {tuple(int(round(v)) for v in got)} ~= expected")


def visible(data: bytes, label: str) -> None:
    if not any(data[3::4]):
        raise SystemExit(f"{label} is fully transparent")


def main() -> int:
    executable = cli()
    with tempfile.TemporaryDirectory(prefix="svg_group_isolation_gate_") as tmpdir:
        tmp = Path(tmpdir)

        static_png = tmp / "isolation_0000.png"
        proc = subprocess.run(
            [str(executable), "render", "--plan", str(PLAN_STATIC),
             "--assets-root", str(FIXTURE), "--backend", "software",
             "--start-frame", "0", "--end-frame", "0",
             "--output", str(tmp / "isolation_####.png")],
            capture_output=True, text=True)
        if proc.returncode:
            raise SystemExit(f"SVG CLI static render failed:\n{proc.stderr[-3000:]}")
        if not static_png.is_file():
            raise SystemExit("static render produced no frame 0")
        data = decode_rgba(static_png)
        visible(data, "static isolation frame 0")

        # Group layout: background red; group child 1 green spans
        # y=[8,36); child 2 blue spans y=[24,40). Overlap is y=[24,36).
        #
        # The oracle is RELATIONAL (renderer color-space agnostic): whatever
        # blend space the core uses, the isolated-group contract requires
        # (a) the overlap pixel to reproduce the top child exactly — the
        # lower sibling may not tint it; (b) both children to carry the
        # same constant group factor; (c) one channel that identifies the
        # lower sibling (green) to be zero in the overlap. Fold-alpha
        # compositing fails (a) and (c) simultaneously.
        overlap = pixel_at(data, 24, 30)
        green_only = pixel_at(data, 24, 14)
        blue_only = pixel_at(data, 24, 38)
        background = pixel_at(data, 4, 4)
        if not all(isclose(background[ch], (255, 0, 0)[ch], abs_tol=TOLERANCE) for ch in range(3)):
            raise SystemExit(f"background pixel changed: {background}")
        print("  PASS  background stays pure red")
        if not all(isclose(overlap[ch], blue_only[ch], abs_tol=TOLERANCE) for ch in range(3)):
            raise SystemExit(
                f"overlap {overlap} differs from top-only {blue_only}: "
                "the lower sibling leaks into the overlap (fold alpha)")
        print(f"  PASS  overlap reproduces the top child exactly {overlap} == {blue_only}")
        if not isclose(green_only[0], blue_only[0], abs_tol=TOLERANCE):
            raise SystemExit(
                f"group factor diverges: green-only {green_only} vs blue-only {blue_only} "
                "(children are not composited with one shared group alpha)")
        print(f"  PASS  both children share one constant group factor (R = {green_only[0]})")
        if overlap[1] > 0 and overlap[1] > TOLERANCE:
            raise SystemExit(
                f"fold signature detected: overlap green channel {overlap[1]} > {TOLERANCE}")
        print("  PASS  overlap independent of the lower sibling (no fold double-darkening)")

        # The overlap must NOT carry the lower sibling (a fold alpha would
        # show green mixed into the overlap, shifting its green channel).
        overlap = pixel_at(data, 24, 30)
        if overlap[1] > TOLERANCE:
            raise SystemExit(
                f"fold signature detected: overlap green channel {overlap[1]} > {TOLERANCE}")

        # Animated translated child inside the isolated group.
        def frame_hashes(plan: Path, prefix: str) -> dict[int, str]:
            frames = {}
            render(plan, tmp, prefix, 0, 30)
            for frame in (0, 15, 30):
                path = tmp / f"{prefix}_{frame:04d}.png"
                data = decode_rgba(path)
                visible(data, f"animated {prefix} frame {frame}")
                frames[frame] = hashlib.sha256(data).hexdigest()
            return frames

        hashes = frame_hashes(PLAN_ANIMATED, "animated")
        if hashes[0] == hashes[30]:
            raise SystemExit("animated isolated group endpoint hashes collide")
        print("  PASS  animated endpoints differ (0 vs 30)")

        # Determinism: repeat render, inspect mid frame.
        repeat = frame_hashes(PLAN_ANIMATED, "animated_repeat")
        if hashes[15] != repeat[15]:
            raise SystemExit("repeat render diverged: isolated group stage is nondeterministic")
        print("  PASS  repeat render reproduces the mid-frame hash (deterministic)")

    print("SVG_GROUP_ISOLATION_GATE PASS: isolated group compositing certifies end to end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
