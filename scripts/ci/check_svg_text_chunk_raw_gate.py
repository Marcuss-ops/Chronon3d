#!/usr/bin/env python3
"""Check that paint-only SVG text chunks preserve shared shaping and paint."""
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
ARABIC_FONT = ROOT / "assets" / "fonts" / "NotoNaskhArabic-Bold.ttf"
WIDTH, HEIGHT = 256, 128
FRAME_BYTES = WIDTH * HEIGHT * 4


def render(cli: Path, root: Path, stem: str, chunked: bool,
           ligature_boundary: bool = False, mixed_script: bool = False,
           rtl_script: bool = False, grapheme_boundary: bool = False) -> bytes:
    if grapheme_boundary:
        text = 'e<tspan fill="#ff0000">\u0301</tspan>x' if chunked else "e\u0301x"
    elif ligature_boundary:
        text = 'f<tspan fill="#ff0000">i</tspan>'
    elif rtl_script:
        text = 'س<tspan fill="#ff0000">ل</tspan>ام' if chunked else "سلام"
    elif mixed_script:
        text = 'A<tspan fill="#ff0000">Ω</tspan>Ж' if chunked else "AΩЖ"
    else:
        text = 'A<tspan fill="#ff0000">V</tspan>' if chunked else "AV"
    family = "Noto Naskh Arabic" if rtl_script else "Inter"
    direction = ' direction="rtl"' if rtl_script else ""
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <text x="20" y="90" font-family="{family}" font-weight="bold" font-size="64" fill="#000000"{direction}>{text}</text>
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
    if stem == "grapheme_chunked":
        print((result.stdout + result.stderr)[-3000:])
    pixels = raw.read_bytes()
    if len(pixels) != FRAME_BYTES:
        raise RuntimeError(f"{stem}: expected {FRAME_BYTES} RGBA bytes, got {len(pixels)}")
    return pixels


def render_canonical_ligature(cli: Path, root: Path, stem: str) -> bytes:
    """Exercise TextSpanOverride inside one canonical text layout."""
    plan = {
        "schema": "chronon.render-plan.v3", "version": 3, "job_id": stem,
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps_num": 30,
                   "fps_den": 1, "duration_frames": 1},
        "layers": [{
            "id": "canonical_ligature", "type": "text", "text": "fi",
            "font": "fonts/Inter-Bold.ttf", "position": [128, 64],
            "size": [128, 96],
            "style": {"font": "fonts/Inter-Bold.ttf", "font_family": "Inter",
                      "font_size": 64, "fill": "#000000", "fit_mode": "none"},
            "spans": [
                {"start": 0, "end": 1, "style": {"color": "#000000"}},
                {"start": 1, "end": 2, "style": {"color": "#ff0000"}},
            ],
        }],
        "output": {"path": f"{stem}.mp4", "format": "mp4"},
    }
    plan_path = root / f"{stem}.json"
    plan_path.write_text(json.dumps(plan))
    raw = root / f"{stem}.rgba"
    result = subprocess.run([
        str(cli), "render", "--plan", str(plan_path), "--assets-root", str(root),
        "--backend", "software", "--video-sink", "raw", "--output", str(raw),
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
    if not cli.is_file() or not FONT.is_file() or not ARABIC_FONT.is_file():
        parser.error(f"CLI or bundled text test fonts are missing: {cli}, {FONT}, {ARABIC_FONT}")
    with tempfile.TemporaryDirectory(prefix="svg_text_chunk_") as temp:
        root = Path(temp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "Inter-Bold.ttf")
        shutil.copyfile(ARABIC_FONT,
                        root / "fonts" / "Noto-Naskh-Arabic-Bold.ttf")
        plain = render(cli, root, "plain", False)
        chunked = render(cli, root, "chunked", True)
        repeat = render(cli, root, "chunked_repeat", True)
        ligature = render(cli, root, "ligature_boundary", True, True)
        ligature_repeat = render(cli, root, "ligature_boundary_repeat", True, True)
        canonical_ligature = render_canonical_ligature(cli, root, "canonical_ligature")
        canonical_ligature_repeat = render_canonical_ligature(
            cli, root, "canonical_ligature_repeat")
        mixed_plain = render(cli, root, "mixed_script_plain", False, mixed_script=True)
        mixed_chunked = render(cli, root, "mixed_script_chunked", True, mixed_script=True)
        mixed_repeat = render(cli, root, "mixed_script_repeat", True, mixed_script=True)
        rtl_plain = render(cli, root, "rtl_plain", False, rtl_script=True)
        rtl_chunked = render(cli, root, "rtl_chunked", True, rtl_script=True)
        rtl_repeat = render(cli, root, "rtl_repeat", True, rtl_script=True)
        grapheme_plain = render(cli, root, "grapheme_plain", False, grapheme_boundary=True)
        grapheme_chunked = render(cli, root, "grapheme_chunked", True, grapheme_boundary=True)
        grapheme_repeat = render(cli, root, "grapheme_repeat", True, grapheme_boundary=True)

    alpha_plain = [plain[index + 3] for index in range(0, FRAME_BYTES, 4)]
    alpha_chunked = [chunked[index + 3] for index in range(0, FRAME_BYTES, 4)]
    changed = sum(abs(a - b) > 16 for a, b in zip(alpha_plain, alpha_chunked))
    if changed > 2:
        raise RuntimeError(f"paint-only tspan changed {changed} alpha pixels beyond AA tolerance")
    if chunked != repeat:
        raise RuntimeError("repeated styled chunk render differs pixel-for-pixel")
    if ligature != ligature_repeat:
        raise RuntimeError("repeated ligature-boundary chunk render differs pixel-for-pixel")
    if canonical_ligature != canonical_ligature_repeat:
        raise RuntimeError("repeated canonical ligature-boundary render differs pixel-for-pixel")
    mixed_alpha_plain = [mixed_plain[i + 3] for i in range(0, FRAME_BYTES, 4)]
    mixed_alpha_chunked = [mixed_chunked[i + 3] for i in range(0, FRAME_BYTES, 4)]
    mixed_changed = sum(abs(a - b) > 16 for a, b in zip(mixed_alpha_plain, mixed_alpha_chunked))
    if mixed_changed > 2:
        raise RuntimeError(
            f"mixed Latin/Greek/Cyrillic paint chunks changed {mixed_changed} alpha pixels")
    if mixed_chunked != mixed_repeat:
        raise RuntimeError("mixed-script styled text render is not deterministic")
    rtl_alpha_plain = [rtl_plain[i + 3] for i in range(0, FRAME_BYTES, 4)]
    rtl_alpha_chunked = [rtl_chunked[i + 3] for i in range(0, FRAME_BYTES, 4)]
    rtl_changed = sum(abs(a - b) > 16 for a, b in zip(rtl_alpha_plain, rtl_alpha_chunked))
    if rtl_changed > 2:
        raise RuntimeError(f"RTL Arabic paint chunks changed {rtl_changed} alpha pixels")
    if rtl_chunked != rtl_repeat:
        raise RuntimeError("RTL styled text render is not deterministic")
    grapheme_alpha_plain = [grapheme_plain[i + 3] for i in range(0, FRAME_BYTES, 4)]
    grapheme_alpha_chunked = [grapheme_chunked[i + 3] for i in range(0, FRAME_BYTES, 4)]
    grapheme_changed = sum(abs(a - b) > 16 for a, b in zip(
        grapheme_alpha_plain, grapheme_alpha_chunked))
    if grapheme_changed > 2:
        def bounds(frame):
            points = [(i // 4 % WIDTH, i // (4 * WIDTH))
                      for i in range(0, FRAME_BYTES, 4) if frame[i + 3] > 32]
            return (min(x for x, _ in points), min(y for _, y in points),
                    max(x for x, _ in points), max(y for _, y in points)) if points else None
        raise RuntimeError(
            f"combining-mark paint chunk changed grapheme geometry at {grapheme_changed} pixels "
            f"(plain bounds={bounds(grapheme_plain)}, chunked bounds={bounds(grapheme_chunked)})")
    if grapheme_chunked != grapheme_repeat:
        raise RuntimeError("combining-mark paint chunk render is not deterministic")
    grapheme_visible = [grapheme_chunked[i + 3]
                        for i in range(0, FRAME_BYTES, 4)
                        if grapheme_chunked[i + 3] > 32]
    if len(grapheme_visible) < 100:
        raise RuntimeError(
            f"combining-mark chunk lost its grapheme ({len(grapheme_visible)} visible pixels)")
    grapheme_red = sum(
        grapheme_chunked[index] > grapheme_chunked[index + 3] * 0.8
        for index in range(0, FRAME_BYTES, 4)
        if grapheme_chunked[index + 3] > 32)
    if grapheme_red < 10:
        raise RuntimeError(
            "combining-mark tspan preserved grapheme geometry but lost its red paint "
            f"(red pixels={grapheme_red})")
    grapheme_black = sum(
        grapheme_chunked[index + 3] > 32 and
        grapheme_chunked[index] < 64 and
        grapheme_chunked[index + 1] < 64 and
        grapheme_chunked[index + 2] < 64
        for index in range(0, FRAME_BYTES, 4))
    if grapheme_black < 100:
        raise RuntimeError(
            "combining-mark paint overlay recolored the base glyph "
            f"(black pixels={grapheme_black})")

    visible = [(index // 4 % WIDTH, chunked[index], chunked[index + 1],
                chunked[index + 2], chunked[index + 3])
               for index in range(0, FRAME_BYTES, 4) if chunked[index + 3] > 32]
    if len(visible) < 500:
        raise RuntimeError(f"styled text produced too few visible pixels: {len(visible)}")
    left_x = min(item[0] for item in visible)
    right_x = max(item[0] for item in visible)
    midpoint = (left_x + right_x) // 2
    left = [item for item in visible if item[0] <= midpoint]
    right = [item for item in visible if item[0] > midpoint]
    left_black = sum(r < alpha * 0.2 for _, r, _, _, alpha in left)
    right_red = sum(r > alpha * 0.8 for _, r, _, _, alpha in right)
    if left_black < len(left) * 0.5:
        raise RuntimeError(f"first A chunk did not retain black paint ({left_black}/{len(left)} black pixels)")
    if right_red < len(right) * 0.8:
        raise RuntimeError(f"second V chunk did not retain red paint ({right_red}/{len(right)} red pixels)")
    ligature_visible = [(ligature[index], ligature[index + 1],
                         ligature[index + 2], ligature[index + 3])
                        for index in range(0, FRAME_BYTES, 4)
                        if ligature[index + 3] > 32]
    ligature_black = sum(r < alpha * 0.2 for r, _, _, alpha in ligature_visible)
    ligature_red = sum(r > alpha * 0.8 for r, _, _, alpha in ligature_visible)
    if len(ligature_visible) < 400 or ligature_black < 100 or ligature_red < 100:
        raise RuntimeError(
            "paint split inside the 'fi' ligature lost a character or paint range "
            f"(visible={len(ligature_visible)}, black={ligature_black}, red={ligature_red})")
    canonical_visible = [(canonical_ligature[index], canonical_ligature[index + 1],
                          canonical_ligature[index + 2], canonical_ligature[index + 3])
                         for index in range(0, FRAME_BYTES, 4)
                         if canonical_ligature[index + 3] > 32]
    canonical_black = sum(r < alpha * 0.2 for r, _, _, alpha in canonical_visible)
    canonical_red = sum(r > alpha * 0.8 for r, _, _, alpha in canonical_visible)
    if len(canonical_visible) < 400 or canonical_black < 100 or canonical_red < 100:
        raise RuntimeError(
            "canonical text shaping lost paint across an 'fi' ligature boundary "
            f"(visible={len(canonical_visible)}, black={canonical_black}, red={canonical_red})")
    mixed_visible = [(mixed_chunked[i], mixed_chunked[i + 1], mixed_chunked[i + 2],
                      mixed_chunked[i + 3]) for i in range(0, FRAME_BYTES, 4)
                     if mixed_chunked[i + 3] > 32]
    mixed_red = sum(r > alpha * 0.8 for r, _, _, alpha in mixed_visible)
    if len(mixed_visible) < 300 or mixed_red < 50:
        raise RuntimeError(
            f"mixed-script text lost glyphs or its middle paint range "
            f"(visible={len(mixed_visible)}, red={mixed_red})")
    rtl_visible = [(rtl_chunked[i], rtl_chunked[i + 1], rtl_chunked[i + 2],
                    rtl_chunked[i + 3]) for i in range(0, FRAME_BYTES, 4)
                   if rtl_chunked[i + 3] > 32]
    rtl_red = sum(r > alpha * 0.8 for r, _, _, alpha in rtl_visible)
    if len(rtl_visible) < 300 or rtl_red < 50:
        raise RuntimeError(
            f"RTL Arabic text lost contextual glyphs or its middle paint range "
            f"(visible={len(rtl_visible)}, red={rtl_red})")
    print(f"SVG styled chunk shaping: PASS alpha differs at {changed} pixels (tolerance 16)")
    print(f"SVG styled chunk paint: PASS A black={left_black}/{len(left)}, V red={right_red}/{len(right)}")
    print("SVG styled chunk determinism: PASS (RGBA frame matches exactly)")
    print(f"SVG ligature-boundary paint: PASS black={ligature_black}, red={ligature_red}; deterministic")
    print(f"Canonical ligature-boundary paint: PASS black={canonical_black}, red={canonical_red}; deterministic")
    print(f"Mixed-script chunk shaping: PASS alpha differs at {mixed_changed} pixels; red={mixed_red}; deterministic")
    print(f"RTL Arabic chunk shaping: PASS alpha differs at {rtl_changed} pixels; red={rtl_red}; deterministic")
    print(f"Combining grapheme chunk: PASS alpha differs at {grapheme_changed} pixels; "
          f"visible={len(grapheme_visible)} black={grapheme_black} "
          f"red={grapheme_red}; deterministic")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"SVG text chunk gate failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
