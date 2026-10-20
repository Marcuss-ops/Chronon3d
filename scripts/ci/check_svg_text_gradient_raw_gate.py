#!/usr/bin/env python3
"""Verify imported SVG text gradients reach deterministic software pixels."""
from __future__ import annotations

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


def render(cli: Path, root: Path, stem: str, stroke: bool = False) -> bytes:
    paint = ('fill="none" stroke="url(#paint)" stroke-width="4"' if stroke
             else 'fill="url(#paint)"')
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <defs><linearGradient id="paint"><stop offset="0" stop-color="#FF0000"/><stop offset="1" stop-color="#0000FF"/></linearGradient></defs>
      <text x="128" y="90" text-anchor="middle" font-family="Inter" font-weight="bold" font-size="64" {paint}>SVG</text>
    </svg>'''
    svg_path = root / f"{stem}.svg"
    svg_path.write_text(svg)
    plan_path = root / f"{stem}.plan.json"
    plan_path.write_text(json.dumps({
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": stem, "canvas": {"width": WIDTH, "height": HEIGHT,
        "fps_num": 30, "fps_den": 1, "duration_frames": 1},
        "layers": [{"id": "svg_text", "type": "shape",
                    "position": [0, 0], "size": [WIDTH, HEIGHT],
                    "shape": {"type": "path", "asset": svg_path.name}}],
        "output": {"path": f"{stem}.mp4", "format": "mp4"},
    }))
    raw_path = root / f"{stem}.rgba"
    result = subprocess.run([
        str(cli), "render", "--plan", str(plan_path), "--assets-root", str(root),
        "--backend", "software", "--video-sink", "raw", "--output", str(raw_path),
    ], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError((result.stdout + "\n" + result.stderr)[-6000:])
    pixels = raw_path.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"text gradient render returned {len(pixels)} bytes, expected {FRAME_BYTES}")
    return pixels


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=os.environ.get("CHRONON_CLI"))
    args = parser.parse_args()
    if not args.cli:
        parser.error("pass --cli or set CHRONON_CLI to an SVG-enabled chronon3d_cli")
    cli = Path(args.cli).resolve()
    if not cli.is_file() or not FONT.is_file():
        parser.error(f"CLI or bundled Inter test font is missing: {cli}, {FONT}")
    with tempfile.TemporaryDirectory(prefix="svg_text_gradient_") as tmp:
        root = Path(tmp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        first = render(cli, root, "gradient_a")
        repeat = render(cli, root, "gradient_b")
        stroke = render(cli, root, "stroke_gradient_a", stroke=True)
        stroke_repeat = render(cli, root, "stroke_gradient_b", stroke=True)
    visible = [(x, r, g, b, a) for y in range(HEIGHT) for x in range(WIDTH)
               for r, g, b, a in [first[(y * WIDTH + x) * 4:(y * WIDTH + x) * 4 + 4]] if a > 32]
    if len(visible) < 500:
        raise RuntimeError(f"SVG text gradient produced too few visible pixels: {len(visible)}")
    xs = [pixel[0] for pixel in visible]
    midpoint = (min(xs) + max(xs)) // 2
    left = [pixel for pixel in visible if pixel[0] < midpoint]
    right = [pixel for pixel in visible if pixel[0] >= midpoint]
    red_left = sum(r for _, r, _, _, _ in left) / max(1, len(left))
    blue_left = sum(b for _, _, _, b, _ in left) / max(1, len(left))
    red_right = sum(r for _, r, _, _, _ in right) / max(1, len(right))
    blue_right = sum(b for _, _, _, b, _ in right) / max(1, len(right))
    if red_left <= blue_left * 1.25 or blue_right <= red_right * 1.25:
        raise RuntimeError(f"gradient direction missing: left n={len(left)} R/B={red_left:.1f}/{blue_left:.1f}, right n={len(right)} R/B={red_right:.1f}/{blue_right:.1f}, x={min(xs)}..{max(xs)}")
    if first != repeat:
        raise RuntimeError("repeated SVG text gradient raster differs pixel-for-pixel")
    print(f"SVG text gradient direction: PASS left R/B={red_left:.1f}/{blue_left:.1f}, right R/B={red_right:.1f}/{blue_right:.1f}")
    print("SVG text gradient determinism: PASS (RGBA frame matches exactly)")
    stroke_visible = [(x, r, g, b, a) for y in range(HEIGHT) for x in range(WIDTH)
                      for r, g, b, a in [stroke[(y * WIDTH + x) * 4:(y * WIDTH + x) * 4 + 4]] if a > 32]
    if len(stroke_visible) < 200 or stroke != stroke_repeat:
        raise RuntimeError("SVG text stroke gradient is missing or nondeterministic")
    stroke_xs = [pixel[0] for pixel in stroke_visible]
    stroke_mid = (min(stroke_xs) + max(stroke_xs)) // 2
    stroke_left = [pixel for pixel in stroke_visible if pixel[0] < stroke_mid]
    stroke_right = [pixel for pixel in stroke_visible if pixel[0] >= stroke_mid]
    slr = sum(p[1] for p in stroke_left) / max(1, len(stroke_left))
    slb = sum(p[3] for p in stroke_left) / max(1, len(stroke_left))
    srr = sum(p[1] for p in stroke_right) / max(1, len(stroke_right))
    srb = sum(p[3] for p in stroke_right) / max(1, len(stroke_right))
    if slr <= slb * 1.25 or srb <= srr * 1.25:
        raise RuntimeError(f"SVG text stroke gradient direction missing: left R/B={slr:.1f}/{slb:.1f}, right R/B={srr:.1f}/{srb:.1f}")
    print(f"SVG text stroke gradient: PASS left R/B={slr:.1f}/{slb:.1f}, right R/B={srr:.1f}/{srb:.1f}; deterministic")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text gradient gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
