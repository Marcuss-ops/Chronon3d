#!/usr/bin/env python3
"""Render repeated SVG text patterns through the software glyph path."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "assets" / "fonts" / "Inter-Bold.ttf"
WIDTH, HEIGHT = 320, 160
FRAME_BYTES = WIDTH * HEIGHT * 4


def render(cli: Path, root: Path, stem: str) -> bytes:
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <defs><pattern id="tile" patternUnits="userSpaceOnUse" patternContentUnits="userSpaceOnUse" width="8" height="8" patternTransform="translate(1 0)">
        <rect x="0" y="0" width="4" height="8" fill="#ff0000"/>
        <rect x="4" y="0" width="4" height="8" fill="#0000ff"/>
        <path d="M 3 1 L 1 7 L 5 7 Z" transform="translate(1 0)" fill="#00ff00"/>
        <rect x="0" y="0" width="4" height="8" fill="#ffffff" opacity="0.5" fill-opacity="0.5"/>
      </pattern></defs>
      <text x="16" y="70" font-family="Inter" font-weight="bold" font-size="64" fill="url(#tile)">PATTERN</text>
      <text x="16" y="142" font-family="Inter" font-weight="bold" font-size="64" fill="none" stroke="url(#tile)" stroke-width="3">SVG</text>
    </svg>'''
    (root / f"{stem}.svg").write_text(svg)
    (root / f"{stem}.json").write_text(json.dumps({
        "schema": "chronon.render-plan.v3", "version": 3, "job_id": stem,
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps_num": 30,
                   "fps_den": 1, "duration_frames": 1},
        "layers": [{"id": "svg_text", "type": "shape", "position": [0, 0],
                    "size": [WIDTH, HEIGHT],
                    "shape": {"type": "path", "asset": f"{stem}.svg"}}],
        "output": {"path": f"{stem}.mp4", "format": "mp4"},
    }))
    raw = root / f"{stem}.rgba"
    result = subprocess.run([
        str(cli), "render", "--plan", str(root / f"{stem}.json"),
        "--assets-root", str(root), "--backend", "software",
        "--video-sink", "raw", "--output", str(raw),
    ], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    pixels = raw.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"{stem}: expected {FRAME_BYTES} RGBA bytes, got {len(pixels)}")
    return pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=os.environ.get("CHRONON_CLI"))
    args = parser.parse_args()
    if not args.cli:
        parser.error("pass --cli or set CHRONON_CLI to an SVG-enabled chronon3d_cli")
    cli = Path(args.cli).resolve()
    if not cli.is_file() or not FONT.is_file():
        parser.error(f"CLI or bundled text font is missing: {cli}, {FONT}")
    with tempfile.TemporaryDirectory(prefix="svg_text_pattern_") as temp:
        root = Path(temp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        frame = render(cli, root, "pattern")
        repeat = render(cli, root, "pattern_repeat")
    visible = [(frame[i], frame[i + 1], frame[i + 2], frame[i + 3])
               for i in range(0, FRAME_BYTES, 4) if frame[i + 3] > 48]
    red = sum(r > g * 1.3 and r > b * 1.3 for r, g, b, _ in visible)
    blue = sum(b > r * 1.3 and b > g * 1.3 for r, g, b, _ in visible)
    green = sum(g > r * 1.3 and g > b * 1.3 for r, g, b, _ in visible)
    source_over = sum(r > g * 1.2 and alpha * 0.4 < g < alpha * 0.7
                      for r, g, _, alpha in visible)
    if len(visible) < 1000 or red < 200 or blue < 200 or green < 100 or source_over < 100:
        raise RuntimeError(
            f"pattern fill/stroke lost tile paint: visible={len(visible)} red={red} "
            f"blue={blue} green_path={green} translucent_source_over={source_over}")
    if frame != repeat:
        raise RuntimeError("repeated SVG text pattern render differs pixel-for-pixel")
    print(f"SVG text pattern fill/stroke: PASS visible={len(visible)} red={red} "
          f"blue={blue} green_path={green} translucent_source_over={source_over}")
    print("SVG text pattern determinism: PASS (RGBA frame matches exactly)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text pattern gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
