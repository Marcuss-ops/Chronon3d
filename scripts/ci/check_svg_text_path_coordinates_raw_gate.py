#!/usr/bin/env python3
"""Verify SVG textPath dx/dy lists affect shaped-cluster placement."""
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


def render(cli: Path, root: Path, name: str, positioned: bool,
           coordinates: str = "", text_transform: str = "") -> bytes:
    attrs = 'dx="0 12 0 0 0 0" dy="0 6 0 0 0 0"' if positioned else ""
    (root / f"{name}.svg").write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <defs><path id="route" d="M 16 64 L 240 64"/></defs>
      <text {text_transform} font-family="Inter" font-weight="bold" font-size="32"><textPath href="#route" startOffset="8" {attrs} {coordinates}>SVG PATH</textPath></text>
    </svg>''')
    plan_path = root / f"{name}.plan.json"
    plan_path.write_text(json.dumps({
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": name, "canvas": {"width": WIDTH, "height": HEIGHT,
        "fps_num": 30, "fps_den": 1, "duration_frames": 1},
        "layers": [{"id": "svg_text", "type": "shape", "position": [0, 0],
                    "size": [WIDTH, HEIGHT], "shape": {"type": "path", "asset": f"{name}.svg"}}],
        "output": {"path": f"{name}.mp4", "format": "mp4"},
    }))
    raw_path = root / f"{name}.rgba"
    result = subprocess.run([str(cli), "render", "--plan", str(plan_path),
        "--assets-root", str(root), "--backend", "software", "--video-sink", "raw",
        "--output", str(raw_path)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    pixels = raw_path.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"textPath render returned {len(pixels)} bytes, expected {FRAME_BYTES}")
    return pixels


def render_transformed_lists(cli: Path, root: Path, name: str, rotated: bool) -> bytes:
    transform = 'transform="rotate(20 128 64)"' if rotated else ""
    (root / f"{name}.svg").write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <text {transform} x="48 96 144" y="58 58 58" font-family="Inter" font-weight="bold" font-size="30">SVG</text>
    </svg>''')
    plan_path = root / f"{name}.plan.json"
    plan_path.write_text(json.dumps({
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": name, "canvas": {"width": WIDTH, "height": HEIGHT,
        "fps_num": 30, "fps_den": 1, "duration_frames": 1},
        "layers": [{"id": "svg_text", "type": "shape", "position": [0, 0],
                    "size": [WIDTH, HEIGHT], "shape": {"type": "path", "asset": f"{name}.svg"}}],
        "output": {"path": f"{name}.mp4", "format": "mp4"},
    }))
    raw_path = root / f"{name}.rgba"
    result = subprocess.run([str(cli), "render", "--plan", str(plan_path),
        "--assets-root", str(root), "--backend", "software", "--video-sink", "raw",
        "--output", str(raw_path)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    pixels = raw_path.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"transformed text render returned {len(pixels)} bytes, expected {FRAME_BYTES}")
    return pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=os.environ.get("CHRONON_CLI"))
    args = parser.parse_args()
    if not args.cli:
        parser.error("pass --cli or set CHRONON_CLI to an SVG-enabled chronon3d_cli")
    cli = Path(args.cli).resolve()
    if not cli.is_file() or not FONT.is_file():
        parser.error(f"CLI or bundled Inter test font is missing: {cli}, {FONT}")
    with tempfile.TemporaryDirectory(prefix="svg_text_path_coordinates_") as tmp:
        root = Path(tmp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        plain = render(cli, root, "plain", False)
        positioned = render(cli, root, "positioned", True)
        repeat = render(cli, root, "positioned_repeat", True)
        absolute = render(cli, root, "absolute", False, 'x="48 96 144" y="64 64 64"')
        absolute_repeat = render(cli, root, "absolute_repeat", False, 'x="48 96 144" y="64 64 64"')
        transformed_absolute = render(cli, root, "transformed_absolute", False,
            'x="48 96 144" y="58 64 70"', 'transform="rotate(20 128 64)"')
        transformed_repeat = render(cli, root, "transformed_repeat", False,
            'x="48 96 144" y="58 64 70"', 'transform="rotate(20 128 64)"')
        axis_aligned = render_transformed_lists(cli, root, "axis_aligned_lists", False)
        rotated_lists = render_transformed_lists(cli, root, "rotated_lists", True)
        rotated_repeat = render_transformed_lists(cli, root, "rotated_lists_repeat", True)
    visible = lambda frame: sum(frame[index] > 32 for index in range(3, len(frame), 4))
    if visible(plain) < 200 or visible(positioned) < 200:
        raise RuntimeError(f"textPath output is not visible: plain={visible(plain)}, positioned={visible(positioned)}")
    if positioned == plain:
        raise RuntimeError("textPath dx/dy lists did not change glyph placement")
    if positioned != repeat:
        raise RuntimeError("repeated textPath coordinate render differs pixel-for-pixel")
    if absolute == plain:
        raise RuntimeError("textPath x/y coordinate lists did not change glyph placement")
    if absolute != absolute_repeat:
        raise RuntimeError("repeated textPath x/y coordinate render differs pixel-for-pixel")
    if transformed_absolute == absolute or transformed_absolute != transformed_repeat:
        raise RuntimeError("transformed textPath x/y lists did not transform positions deterministically")
    print(f"SVG textPath dx/dy placement: PASS ({visible(plain)} -> {visible(positioned)} visible pixels)")
    print("SVG textPath coordinate determinism: PASS (RGBA frame matches exactly)")
    print("SVG textPath x/y coordinate placement: PASS (absolute positions and repeat are stable)")
    print("SVG transformed textPath x/y coordinates: PASS (paired axes transform and repeat stably)")
    if sum(a != b for a, b in zip(axis_aligned, rotated_lists)) < 100:
        raise RuntimeError("rotated SVG x/y coordinate lists did not transform glyph positions")
    if rotated_lists != rotated_repeat:
        raise RuntimeError("repeated rotated-coordinate text raster differs pixel-for-pixel")
    print("SVG rotated x/y coordinate lists: PASS (positions transform and repeat deterministically)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG textPath coordinate gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
