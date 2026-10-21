#!/usr/bin/env python3
"""Raw-pixel gate for bounded SVG text spacing, length, and per-character rotation."""
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
WIDTH, HEIGHT = 480, 160
FRAME_BYTES = WIDTH * HEIGHT * 4


def render(cli: Path, root: Path, stem: str, attrs: str) -> bytes:
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <text x="20" y="108" font-family="Inter" font-weight="bold" font-size="64" fill="#000000" {attrs}>SVG SVG</text>
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
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-5000:])
    pixels = raw.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"{stem}: expected {FRAME_BYTES} RGBA bytes, got {len(pixels)}")
    return pixels


def bbox(frame: bytes) -> tuple[int, int, int, int] | None:
    points = [(i // 4 % WIDTH, i // (4 * WIDTH))
              for i in range(0, len(frame), 4) if frame[i + 3] > 32]
    if not points:
        return None
    return min(x for x, _ in points), min(y for _, y in points), \
        max(x for x, _ in points), max(y for _, y in points)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=os.environ.get("CHRONON_CLI"))
    args = parser.parse_args()
    if not args.cli:
        parser.error("pass --cli or set CHRONON_CLI to an SVG-enabled chronon3d_cli")
    cli = Path(args.cli).resolve()
    if not cli.is_file() or not FONT.is_file():
        parser.error(f"CLI or bundled Inter font is missing: {cli}, {FONT}")
    cases = {
        "plain": "",
        "letter": 'letter-spacing="8px"',
        "word": 'word-spacing="16px"',
        "length_spacing": 'textLength="360" lengthAdjust="spacing"',
        "length_glyphs": 'textLength="360" lengthAdjust="spacingAndGlyphs"',
        "rotate": 'rotate="0 0 20 0 0 0 0"',
    }
    with tempfile.TemporaryDirectory(prefix="svg_text_spacing_") as temp:
        root = Path(temp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        frames = {name: render(cli, root, name, attrs) for name, attrs in cases.items()}
        repeated = render(cli, root, "rotate_repeat", cases["rotate"])
    boxes = {name: bbox(frame) for name, frame in frames.items()}
    if any(value is None for value in boxes.values()):
        raise RuntimeError(f"a text case produced no pixels: {boxes}")
    widths = {name: box[2] - box[0] + 1 for name, box in boxes.items() if box}
    if widths["letter"] < widths["plain"] + 25:
        raise RuntimeError(f"letter-spacing did not expand the text: {widths}")
    if widths["word"] < widths["plain"] + 8:
        raise RuntimeError(f"word-spacing did not expand the word gap: {widths}")
    if widths["length_spacing"] < widths["plain"] + 40:
        raise RuntimeError(f"textLength spacing did not reach the requested length: {widths}")
    if widths["length_glyphs"] < widths["plain"] + 40:
        raise RuntimeError(f"textLength spacingAndGlyphs did not scale glyphs: {widths}")
    if frames["rotate"] == frames["plain"]:
        raise RuntimeError("per-character rotate did not change rendered pixels")
    if frames["rotate"] != repeated:
        raise RuntimeError("per-character rotate render is not deterministic")
    print(f"SVG text spacing/length: PASS widths={widths}")
    print("SVG per-character rotate and repeat determinism: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text spacing raw gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
