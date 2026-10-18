#!/usr/bin/env python3
"""End-to-end encoded-pixel gate for imported SVG motion operators.

Render a resolver-backed SVG through the canonical RenderPlan CLI. Check
operator-specific pixels, compare Offset/Wiggle against plans without those
operators, and verify distinct, repeatable MP4 endpoint frames.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "svg_cli_gate"
PLAN = FIXTURE / "plan.json"
SVG = FIXTURE / "animated.svg"
KEYFRAMES = (0, 10)
MAX_INPUT_BYTES = 8 * 1024 * 1024


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    candidates = (
        ROOT / "build" / "chronon" / "svg-roadmap" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-video-test" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-fast-dev" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-ci-full-validation" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("chronon3d_cli not found (set CHRONON_CLI to an SVG-enabled build)")


def run_cli(command: str, *arguments: str) -> None:
    proc = subprocess.run([str(cli()), command, *arguments], capture_output=True, text=True)
    if proc.returncode:
        output = (proc.stdout + "\n" + proc.stderr).strip()
        raise SystemExit(f"SVG CLI {command} failed:\n{output[-3000:]}")


def render_endpoint_hashes(executable: Path, output_video: Path,
                          plan_path: Path = PLAN, verify_pixels: bool = True) -> dict[int, str]:
    proc = subprocess.run(
        [str(executable), "render", "--plan", str(plan_path), "--assets-root", str(FIXTURE),
         "--backend", "software", "--start-frame", "0", "--end-frame", str(max(KEYFRAMES)),
         "--output", str(output_video)], capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"SVG CLI render failed:\n{proc.stderr[-3000:]}")
    if not output_video.is_file():
        raise SystemExit(f"SVG CLI did not write the expected video: {output_video}")
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(output_video), "-f", "rawvideo",
         "-pix_fmt", "rgba", "-"], capture_output=True, check=True)
    frame_bytes = 64 * 64 * 4
    if len(proc.stdout) % frame_bytes:
        raise SystemExit("decoded render has a partial 64x64 RGBA frame")
    frames = [proc.stdout[offset:offset + frame_bytes]
              for offset in range(0, len(proc.stdout), frame_bytes)]
    if len(frames) != max(KEYFRAMES) + 1:
        raise SystemExit(f"render has {len(frames)} frames; expected {max(KEYFRAMES) + 1}")
    hashes = {}
    cyan_counts = {}
    magenta_counts = {}
    green_counts = {}
    blue_counts = {}
    orange_counts = {}
    lime_counts = {}
    purple_counts = {}
    white_counts = {}
    pink_counts = {}
    for frame in KEYFRAMES:
        pixels = frames[frame]
        red_pixels = 0
        yellow_pixels = 0
        cyan_pixels = 0
        magenta_pixels = 0
        green_pixels = 0
        blue_pixels = 0
        orange_pixels = 0
        lime_pixels = 0
        purple_pixels = 0
        white_pixels = 0
        pink_pixels = 0
        for offset in range(0, len(pixels), 4):
            red, green, blue = pixels[offset:offset + 3]
            pixel_index = offset // 4
            x, y = pixel_index % 64, pixel_index // 64
            red_pixels += red > 150 and green < 100 and blue < 100
            yellow_pixels += red > 150 and green > 120 and blue < 100
            cyan_pixels += (28 <= x <= 60 and 28 <= y <= 40 and
                            red < 100 and green > 150 and blue > 150)
            magenta_pixels += red > 150 and green < 100 and blue > 150
            green_pixels += red < 100 and green > 150 and blue < 100
            blue_pixels += red < 100 and green < 150 and blue > 150
            orange_pixels += red > 180 and 70 < green < 180 and blue < 50
            lime_pixels += 70 < red < 180 and green > 180 and blue < 50
            purple_pixels += red > 80 and green < 80 and blue > 180
            white_pixels += red > 200 and green > 200 and blue > 200
            pink_pixels += red > 180 and green < 100 and 70 < blue < 200
        if red_pixels < 50:
            raise SystemExit(
                f"frame {frame} has only {red_pixels} red control-shape pixels")
        if yellow_pixels < 50:
            raise SystemExit(
                f"frame {frame} has only {yellow_pixels} yellow SVG pixels")
        cyan_counts[frame] = cyan_pixels
        magenta_counts[frame] = magenta_pixels
        green_counts[frame] = green_pixels
        blue_counts[frame] = blue_pixels
        orange_counts[frame] = orange_pixels
        lime_counts[frame] = lime_pixels
        purple_counts[frame] = purple_pixels
        white_counts[frame] = white_pixels
        pink_counts[frame] = pink_pixels
        hashes[frame] = hashlib.sha256(pixels).hexdigest()
    if verify_pixels:
        if cyan_counts[0] > 5 or cyan_counts[10] < 30:
            raise SystemExit(f"Trim Paths reveal did not render as expected: {cyan_counts}")
        if magenta_counts[10] <= magenta_counts[0]:
            raise SystemExit(f"SVG Repeater did not expand its visible pixels: {magenta_counts}")
        if min(green_counts.values()) < 10:
            raise SystemExit(f"SVG Offset Path shape is missing: {green_counts}")
        if min(blue_counts.values()) < 100:
            raise SystemExit(f"SVG Wiggle Path shape is missing: {blue_counts}")
        for kind, counts in (("Round Corners", orange_counts),
                             ("Zig Zag", lime_counts),
                             ("Pucker/Bloat", purple_counts),
                             ("Twist", white_counts),
                             ("Morph", pink_counts)):
            if min(counts.values()) < 10:
                raise SystemExit(f"SVG {kind} output is missing: {counts}")
    return hashes


def main() -> int:
    if SVG.stat().st_size > MAX_INPUT_BYTES:
        raise SystemExit(f"fixture exceeds {MAX_INPUT_BYTES} bytes")
    executable = cli()
    with tempfile.TemporaryDirectory(prefix="svg_cli_gate_") as tmp:
        run_cli("validate", "--plan", str(PLAN), "--assets-root", str(FIXTURE))
        output_root = Path(tmp)
        first_run = render_endpoint_hashes(executable, output_root / "animated-first.mp4")
        hashes = render_endpoint_hashes(executable, output_root / "animated-second.mp4")
        if hashes != first_run:
            raise SystemExit(f"repeat SVG renders changed endpoint hashes: {first_run} != {hashes}")
        plan = json.loads(PLAN.read_text())
        operator_targets = (
            ("offset_path", 1), ("wiggle_path", 1), ("round_corners", 1),
            ("zig_zag", 1), ("pucker_bloat", 1), ("twist", 1),
            ("morph", 1), ("merge", 1),
        )
        for kind, layer_index in operator_targets:
            comparison_plan = json.loads(json.dumps(plan))
            comparison_plan["layers"][layer_index]["shape"]["operators"] = [
                operation for operation in comparison_plan["layers"][layer_index]["shape"]["operators"]
                if operation["kind"] != kind
            ]
            comparison_path = output_root / f"without-{kind}.json"
            comparison_path.write_text(json.dumps(comparison_plan))
            comparison_hashes = render_endpoint_hashes(
                executable, output_root / f"without-{kind}.mp4", comparison_path,
                verify_pixels=False)
            if hashes == comparison_hashes:
                raise SystemExit(f"{kind} produced no encoded-pixel change")
    for frame, digest in hashes.items():
        print(f"SVG frame {frame}: {digest}")
    if len(set(hashes.values())) != len(hashes):
        raise SystemExit("SVG animation endpoint frame hashes collide")
    print("SVG_CLI_GATE PASS: endpoints differ and repeat-render hashes match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
