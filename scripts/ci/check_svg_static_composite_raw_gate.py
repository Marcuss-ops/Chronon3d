#!/usr/bin/env python3
"""Regression gate for a static SVG composed with a moving path marker."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

WIDTH, HEIGHT, FRAMES = 320, 180, 61
FRAME_BYTES = WIDTH * HEIGHT * 4
SAMPLES = (0, 15, 30, 45, 60)
POINTS = ((32.0, 146.0), (96.0, 88.0), (128.0, 76.0),
          (166.0, 40.0), (210.0, 22.0), (278.0, 140.0))


def cli_path(value: str | None) -> Path:
    configured = value or os.environ.get("CHRONON_CLI")
    if not configured:
        raise SystemExit("set CHRONON_CLI or pass --cli to an SVG-enabled build")
    executable = Path(configured).resolve()
    if not executable.is_file():
        raise SystemExit(f"chronon3d_cli not found: {executable}")
    return executable


def svg_source() -> str:
    path = "M " + " L ".join(f"{x:g} {y:g}" for x, y in POINTS)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
            f'height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">'
            f'<path id="peak_line" d="{path}" fill="none" stroke="#E8EBF0" '
            'stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>'
            '</svg>')


def scene_plan() -> dict:
    commands = ([{"type": "move_to", "point": list(POINTS[0])}] +
                [{"type": "line_to", "point": list(point)} for point in POINTS[1:]])
    return {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": "svg_static_composite_raw_gate",
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps_num": 30,
                   "fps_den": 1, "duration_frames": FRAMES},
        "layers": [
            {"id": "peak_svg", "type": "shape", "position": [WIDTH / 2, HEIGHT / 2],
             "size": [WIDTH, HEIGHT], "shape": {"type": "path", "asset": "peak.svg"}},
            {"id": "yellow_marker", "type": "shape", "position": [0.0, 0.0],
             "size": [12.0, 12.0], "shape": {"type": "ellipse", "fill": [1.0, 0.86, 0.08, 1.0]},
             "follow_path": {"path_commands": commands,
                 "progress": {"easing": "linear", "keyframes": [
                     {"frame": 0, "value": 0.0}, {"frame": 60, "value": 1.0}]},
                 "auto_orient": False, "clamp_end": True}},
        ],
        "output": {"path": "svg_static_composite_raw_gate.mp4", "format": "mp4"},
    }


def visible_path_pixels(frame: bytes) -> int:
    return sum(red > 200 and green > 200 and blue > 205 and alpha > 128
               for red, green, blue, alpha in zip(
                   frame[0::4], frame[1::4], frame[2::4], frame[3::4]))


def render(cli: Path, root: Path, label: str) -> bytes:
    plan_path = root / "plan.json"
    plan_path.write_text(json.dumps(scene_plan()))
    output = root / f"{label}.rgba"
    result = subprocess.run(
        [str(cli), "render", "--plan", str(plan_path), "--assets-root", str(root),
         "--backend", "software", "--video-sink", "raw", "--output", str(output)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    raw = output.read_bytes()
    if len(raw) != FRAME_BYTES * FRAMES:
        raise RuntimeError(f"raw sink returned {len(raw)} bytes; expected {FRAME_BYTES * FRAMES}")
    return raw


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    cli = cli_path(parser.parse_args().cli)
    with tempfile.TemporaryDirectory(prefix="svg_static_composite_gate_") as directory:
        root = Path(directory)
        (root / "peak.svg").write_text(svg_source())
        first = render(cli, root, "first")
        second = render(cli, root, "second")
    if first != second:
        raise RuntimeError("repeated static-SVG composition renders differ pixel-for-pixel")
    counts = [visible_path_pixels(first[frame * FRAME_BYTES:(frame + 1) * FRAME_BYTES])
              for frame in SAMPLES]
    if min(counts) < 30:
        raise RuntimeError(f"static SVG path disappeared during marker composition: {counts}")
    print(f"SVG static-composite raw gate: PASS path pixels at f0/15/30/45/60={counts}")
    print("SVG static-composite determinism: PASS (all 61 RGBA frames match)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG static-composite raw gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
