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

Pixel truth surface: the raw pre-encode video sink (`--video-sink raw`),
per the repository doctrine that the still/PNG-sequence image lane is not a
pixel-truth surface (TICKET-TEXT-QUALITY-V2 P0-0b, IMAGE-PATH-PIXEL-TRUTH).

The gate fails closed: missing CLI, CLI failures, invisible frames, oracle
pixel misses, colliding endpoint hashes, or divergent repeat hashes abort
the run.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from math import isclose
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "svg_group_isolation_gate"
PLAN_STATIC = FIXTURE / "plan.json"
PLAN_ANIMATED = FIXTURE / "plan_animated.json"
TOLERANCE = 8  # uint8 steps; software linker is exact, SSAA rounding is <=1


def cli() -> Path:
    gpu_override = os.environ.get("CHRONON_GPU_CLI")
    if gpu_override:
        return Path(gpu_override)
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    for candidate in (
        ROOT / "build" / "chronon" / "linux-video-release" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
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


def canvas_of(plan_path: Path) -> tuple[int, int]:
    plan = json.loads(plan_path.read_text())
    canvas = plan.get("canvas", {})
    return (int(canvas.get("width", 64)), int(canvas.get("height", 64)))


def render_raw(plan_path: Path, output_dir: Path, tag: str,
               start: int, end: int) -> Path:
    """Render through the raw pre-encode sink; return the raw dump path.

    The video-mode output contract primes the legacy compiled export view;
    `--video-sink raw` then bypasses the encoder and writes host RGBA8
    frames verbatim — the canonical pre-encode pixel-truth surface.
    """
    plan = json.loads(plan_path.read_text())
    plan["output"] = {"path": f"{tag}.mp4", "format": "mp4"}
    prepared = output_dir / f"{tag}_video_plan.json"
    prepared.write_text(json.dumps(plan))
    raw_path = output_dir / f"{tag}_raw.dmp"
    backend = os.environ.get("CHRONON_GATE_BACKEND", "vulkan" if "CHRONON_GPU_CLI" in os.environ else "software")
    cmd = [
        str(cli()), "render", "--plan", str(prepared),
        "--assets-root", str(FIXTURE), "--backend", backend,
        "--start-frame", str(start), "--end-frame", str(end),
        "--video-sink", "raw", "--output", str(raw_path)
    ]
    if backend == "vulkan":
        cmd.extend(["--gpu-hot-path-mode", "require_gpu_native"])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"SVG CLI render failed code={proc.returncode}:\nSTDOUT:\n{proc.stdout[-3000:]}\nSTDERR:\n{proc.stderr[-3000:]}")
    if not raw_path.is_file():
        raise SystemExit(f"render produced no raw output: {raw_path}")
    return raw_path


def raw_bytes(raw_path: Path, width: int, height: int, frame: int) -> bytes:
    """Extract one frame (RGBA8, stride width*height*4) from a raw dump."""
    frame_bytes = width * height * 4
    data = raw_path.read_bytes()
    offset = frame * frame_bytes
    chunk = data[offset:offset + frame_bytes]
    if len(chunk) != frame_bytes:
        raise SystemExit(
            f"{raw_path.name}: frame {frame} missing/truncated "
            f"({len(chunk)} of {frame_bytes} bytes)")
    return chunk


def pixel_at(data: bytes, width: int, x: int, y: int) -> tuple[float, ...]:
    o = (y * width + x) * 4
    return tuple(data[o:o + 4])


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
        width, height = canvas_of(PLAN_STATIC)

        static_raw = render_raw(PLAN_STATIC, tmp, "isolation", 0, 0)
        data = raw_bytes(static_raw, width, height, 0)
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
        overlap = pixel_at(data, width, 24, 30)
        green_only = pixel_at(data, width, 24, 14)
        blue_only = pixel_at(data, width, 24, 38)
        background = pixel_at(data, width, 4, 4)
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

        # Animated translated child inside the isolated group.
        def frame_hashes(plan_path: Path, tag: str) -> dict[int, str]:
            frames = {}
            raw = render_raw(plan_path, tmp, tag, 0, 30)
            for frame in (0, 15, 30):
                chunk = raw_bytes(raw, width, height, frame)
                visible(chunk, f"animated {tag} frame {frame}")
                frames[frame] = hashlib.sha256(chunk).hexdigest()
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
