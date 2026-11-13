#!/usr/bin/env python3
"""Raw-pixel gate for bounded SVG text spacing, length, nesting, and rotation."""
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
WIDTH, HEIGHT = 800, 160
FRAME_BYTES = WIDTH * HEIGHT * 4


def render(cli: Path, root: Path, stem: str, attrs: str,
           body: str = "SVG SVG") -> bytes:
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <text x="160" y="108" font-family="Inter" font-weight="bold" font-size="64" {attrs}>{body}</text>
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


def mask_bbox(frame: bytes, red_only: bool = False) -> tuple[int, int, int, int] | None:
    points = []
    for i in range(0, len(frame), 4):
        if frame[i + 3] <= 32:
            continue
        if red_only:
            r, g, b = frame[i], frame[i + 1], frame[i + 2]
            if not (r > 96 and r > g + 48 and r > b + 48):
                continue
        points.append((i // 4 % WIDTH, i // (4 * WIDTH)))
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
        "plain": ("", "SVG SVG"),
        "letter": ('letter-spacing="8px"', "SVG SVG"),
        "word": ('word-spacing="16px"', "SVG SVG"),
        "length_spacing": ('textLength="360" lengthAdjust="spacing"', "SVG SVG"),
        "length_glyphs": ('textLength="360" lengthAdjust="spacingAndGlyphs"', "SVG SVG"),
        "rotate": ('rotate="0 0 20 0 0 0 0"', "SVG SVG"),
        "direct_400": ('textLength="400"', "SVG SVG"),
        "direct_400_glyphs": ('textLength="400" lengthAdjust="spacingAndGlyphs"', "SVG SVG"),
        "direct_420": ('textLength="420"', "SVG SVG"),
        "direct_child_250": ('textLength="250" fill="#ff0000"', "SVG"),
        "direct_child_250_glyphs": (
            'textLength="250" lengthAdjust="spacingAndGlyphs" fill="#ff0000"', "SVG"),
        "nested_parent_spacing": ('textLength="400"', "SVG <tspan>SVG</tspan>"),
        "nested_parent_glyphs": (
            'textLength="400" lengthAdjust="spacingAndGlyphs"',
            "SVG <tspan>SVG</tspan>"),
        "child_plain": ("", 'SVG <tspan fill="#ff0000">SVG</tspan>'),
        "nested_child_spacing": (
            "", 'SVG <tspan textLength="250" fill="#ff0000">SVG</tspan>'),
        "nested_child_glyphs": (
            "", 'SVG <tspan textLength="250" lengthAdjust="spacingAndGlyphs" '
                'fill="#ff0000">SVG</tspan>'),
        "nested_both": (
            'textLength="420"',
            'SVG <tspan textLength="220" fill="#ff0000">SVG</tspan>'),
    }
    with tempfile.TemporaryDirectory(prefix="svg_text_spacing_") as temp:
        root = Path(temp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        frames = {name: render(cli, root, name, attrs, body)
                  for name, (attrs, body) in cases.items()}
        repeated = render(cli, root, "nested_both_repeat", *cases["nested_both"])
        rotate_repeat = render(cli, root, "rotate_repeat", *cases["rotate"])
    boxes = {name: mask_bbox(frame) for name, frame in frames.items()}
    if any(value is None for value in boxes.values()):
        raise RuntimeError(f"a text case produced no pixels: {boxes}")
    widths = {name: box[2] - box[0] + 1 for name, box in boxes.items() if box}
    red_boxes = {
        name: mask_bbox(frames[name], red_only=True)
        for name in ("child_plain", "nested_child_spacing",
                     "nested_child_glyphs", "nested_both")
    }
    if any(value is None for value in red_boxes.values()):
        raise RuntimeError(f"a red child case produced no red pixels: {red_boxes}")
    red_widths = {
        name: box[2] - box[0] + 1 for name, box in red_boxes.items() if box
    }
    direct_child_widths = {
        name: widths[name] for name in ("direct_child_250",
                                        "direct_child_250_glyphs")
    }
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
    if frames["rotate"] != rotate_repeat:
        raise RuntimeError("per-character rotate render is not deterministic")
    if abs(widths["nested_parent_spacing"] - widths["direct_400"]) > 4:
        raise RuntimeError(
            "nested parent spacing does not match the direct run: "
            f"{widths['nested_parent_spacing']} vs {widths['direct_400']}")
    if abs(widths["nested_parent_glyphs"] - widths["direct_400_glyphs"]) > 4:
        raise RuntimeError(
            "nested parent spacingAndGlyphs does not match the direct run: "
            f"{widths['nested_parent_glyphs']} vs {widths['direct_400_glyphs']}")
    if abs(red_widths["nested_child_spacing"] -
           direct_child_widths["direct_child_250"]) > 4:
        raise RuntimeError(
            "nested child spacing does not match the direct child: "
            f"{red_widths['nested_child_spacing']} vs "
            f"{direct_child_widths['direct_child_250']}")
    if abs(red_widths["nested_child_glyphs"] -
           direct_child_widths["direct_child_250_glyphs"]) > 4:
        raise RuntimeError(
            "nested child spacingAndGlyphs does not match the direct child: "
            f"{red_widths['nested_child_glyphs']} vs "
            f"{direct_child_widths['direct_child_250_glyphs']}")
    if red_widths["nested_both"] < red_widths["child_plain"] + 20:
        raise RuntimeError(
            "the nested child constraint did not expand the red child: "
            f"{red_widths['nested_both']} vs {red_widths['child_plain']}")
    if abs(widths["nested_both"] - widths["direct_420"]) > 4:
        raise RuntimeError(
            "nested parent+child does not match the parent target: "
            f"{widths['nested_both']} vs {widths['direct_420']}")
    if frames["nested_both"] != repeated:
        raise RuntimeError("nested parent+child render is not deterministic")
    print(f"SVG text spacing/length: PASS widths={widths}")
    print(f"SVG nested textLength child widths: PASS red={red_widths}")
    print("SVG per-character rotate and repeat determinism: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text spacing raw gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
