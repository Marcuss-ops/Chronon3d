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
WIDTH, HEIGHT = 320, 200
FRAME_BYTES = WIDTH * HEIGHT * 4

BASE_PATTERN = '''<pattern id="tile" patternUnits="userSpaceOnUse" patternContentUnits="userSpaceOnUse" width="8" height="8" patternTransform="translate(1 0)">
        <rect x="0" y="0" width="4" height="8" fill="#ff0000"/>
        <rect x="4" y="0" width="4" height="8" fill="#0000ff"/>
        <path d="M 3 1 L 1 7 L 5 7 Z" transform="translate(1 0)" fill="#00ff00"/>
        <rect x="0" y="0" width="4" height="8" fill="#ffffff" opacity="0.5" fill-opacity="0.5"/>
      </pattern>'''


def render(cli: Path, root: Path, stem: str, body: str) -> bytes:
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      {body}
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


def base_body(pattern_transform: str = "translate(1 0)") -> str:
    pattern = BASE_PATTERN.replace('patternTransform="translate(1 0)"',
                                   f'patternTransform="{pattern_transform}"')
    return f'''<defs>{pattern}</defs>
      <text x="16" y="78" font-family="Inter" font-weight="bold" font-size="64" fill="url(#tile)">PATTERN</text>
      <text x="16" y="160" font-family="Inter" font-weight="bold" font-size="64" fill="none" stroke="url(#tile)" stroke-width="3">SVG</text>'''

EXTENDED_BODY = '''<defs>
        <linearGradient id="ink" gradientUnits="userSpaceOnUse" x1="1" y1="0" x2="5" y2="0">
          <stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/>
        </linearGradient>
        <pattern id="base" patternUnits="userSpaceOnUse" patternContentUnits="userSpaceOnUse" width="8" height="8">
          <g transform="translate(1 0)">
            <rect width="4" height="8" fill="url(#ink)"/>
            <path d="M 0 0 L 2 4" fill="none" stroke="#00ff00" stroke-width="1.5"/>
          </g>
        </pattern>
        <pattern id="derived" href="#base" patternTransform="translate(2 0)"/>
        <pattern id="obb" patternUnits="objectBoundingBox" patternContentUnits="objectBoundingBox" width="0.5" height="0.5">
          <rect width="1" height="1" fill="#ff00ff"/>
        </pattern>
      </defs>
      <text x="16" y="78" font-family="Inter" font-weight="bold" font-size="64" fill="url(#derived)">GROUP</text>
      <text x="16" y="160" font-family="Inter" font-weight="bold" font-size="64" fill="url(#obb)">OBB</text>'''

PHASE_A = base_body("translate(1 0)")
PHASE_B = base_body("translate(3 0)")


def colors(frame: bytes) -> dict[str, int]:
    visible = [(frame[i], frame[i + 1], frame[i + 2], frame[i + 3])
               for i in range(0, FRAME_BYTES, 4) if frame[i + 3] > 48]
    return {
        "visible": len(visible),
        "red": sum(r > g * 1.3 and r > b * 1.3 for r, g, b, _ in visible),
        "blue": sum(b > r * 1.3 and b > g * 1.3 for r, g, b, _ in visible),
        "green": sum(g > r * 1.3 and g > b * 1.3 for r, g, b, _ in visible),
        "magenta": sum(r > g * 1.3 and b > g * 1.3 for r, g, b, _ in visible),
        "source_over": sum(r > g * 1.2 and alpha * 0.4 < g < alpha * 0.7
                           for r, g, _, alpha in visible),
    }


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
        frame = render(cli, root, "pattern", base_body("translate(1 0)"))
        repeat = render(cli, root, "pattern_repeat", base_body("translate(1 0)"))
        phase_a = render(cli, root, "pattern_phase_a", PHASE_A)
        phase_b = render(cli, root, "pattern_phase_b", PHASE_B)
        phase_repeat = render(cli, root, "pattern_phase_repeat", PHASE_B)
        extended = render(cli, root, "pattern_extended", EXTENDED_BODY)
        extended_repeat = render(cli, root, "pattern_extended_repeat", EXTENDED_BODY)
    counts = colors(frame)
    if (counts["visible"] < 1000 or counts["red"] < 200 or
            counts["blue"] < 200 or counts["green"] < 100 or
            counts["source_over"] < 100):
        raise RuntimeError(
            f"pattern fill/stroke lost tile paint: {counts}")
    if frame != repeat:
        raise RuntimeError("repeated SVG text pattern render differs pixel-for-pixel")
    if sum(a != b for a, b in zip(phase_a, phase_b)) < 500:
        raise RuntimeError("patternTransform phase did not change the rendered tile")
    if phase_b != phase_repeat:
        raise RuntimeError("repeated pattern phase render differs pixel-for-pixel")
    extended_counts = colors(extended)
    if (extended_counts["visible"] < 500 or extended_counts["red"] < 100 or
            extended_counts["blue"] < 100 or extended_counts["green"] < 20 or
            extended_counts["magenta"] < 200):
        raise RuntimeError(
            f"group/gradient/stroke/objectBoundingBox pattern paint is missing: "
            f"{extended_counts}")
    if extended != extended_repeat:
        raise RuntimeError("extended SVG text pattern render differs pixel-for-pixel")
    print(f"SVG text pattern fill/stroke: PASS {counts}")
    print("SVG text pattern determinism: PASS (RGBA frame matches exactly)")
    print("SVG text pattern phase: PASS (patternTransform changes tile phase deterministically)")
    print(f"SVG text pattern groups/gradients/strokes/href/objectBoundingBox: PASS {extended_counts}")
    print("SVG text pattern extended determinism: PASS (RGBA frame matches exactly)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text pattern gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
