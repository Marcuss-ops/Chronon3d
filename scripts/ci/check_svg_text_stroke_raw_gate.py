#!/usr/bin/env python3
"""Verify imported SVG text stroke dash styling reaches software-rendered pixels."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "tests" / "fixtures" / "Inter-Bold.ttf"
WIDTH, HEIGHT = 256, 128
FRAME_BYTES = WIDTH * HEIGHT * 4


def plan(svg_name: str) -> dict:
    return {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": "svg_text_stroke_raw_gate",
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps_num": 30,
                   "fps_den": 1, "duration_frames": 1},
        "layers": [{"id": "svg_text", "type": "shape",
                    "position": [WIDTH / 2, HEIGHT / 2],
                    "size": [WIDTH, HEIGHT],
                    "shape": {"type": "path", "asset": svg_name}}],
        "output": {"path": "text_stroke.mp4", "format": "mp4"},
    }


def render(cli: Path, root: Path, name: str, styled: bool) -> bytes:
    stroke_attributes = ('stroke-dasharray="1 18"') if styled else ""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
           f'height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">'
           '<text x="128" y="90" text-anchor="middle" font-family="Inter" '
           'font-weight="bold" font-size="64" fill="none" stroke="#FFE52B" '
           f'stroke-width="4" {stroke_attributes}>SVG</text></svg>')
    svg_name = f"{name}.svg"
    (root / svg_name).write_text(svg)
    plan_path = root / f"{name}.plan.json"
    plan_path.write_text(json.dumps(plan(svg_name)))
    output = root / f"{name}.rgba"
    result = subprocess.run(
        [str(cli), "render", "--plan", str(plan_path), "--assets-root", str(root),
         "--backend", "software", "--video-sink", "raw", "--output", str(output)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    pixels = output.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"text render returned {len(pixels)} bytes, expected {FRAME_BYTES}")
    return pixels


def yellow_pixels(rgba: bytes) -> int:
    return sum(r > 200 and g > 140 and b < 80 and a > 100
               for r, g, b, a in zip(rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=os.environ.get("CHRONON_CLI"))
    args = parser.parse_args()
    if not args.cli:
        parser.error("pass --cli or set CHRONON_CLI to an SVG-enabled chronon3d_cli")
    cli = Path(args.cli).resolve()
    if not cli.is_file() or not FONT.is_file():
        parser.error(f"CLI or bundled Inter test font is missing: {cli}, {FONT}")

    with tempfile.TemporaryDirectory(prefix="svg_text_stroke_") as temporary:
        root = Path(temporary)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        solid = render(cli, root, "solid", styled=False)
        dashed = render(cli, root, "dashed", styled=True)
        repeated = render(cli, root, "dashed_repeat", styled=True)

    solid_count = yellow_pixels(solid)
    dashed_count = yellow_pixels(dashed)
    if solid_count < 500 or dashed_count <= 0 or dashed_count >= solid_count * 0.99:
        raise RuntimeError(f"SVG text dash style did not change stroke pixels: solid={solid_count}, dashed={dashed_count}")
    if dashed != repeated:
        raise RuntimeError("repeated dashed SVG text raster differs pixel-for-pixel")
    print(f"SVG text stroke pixels: PASS solid={solid_count}, dashed={dashed_count}")
    print("SVG text stroke style determinism: PASS (RGBA frame matches exactly)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text stroke gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
