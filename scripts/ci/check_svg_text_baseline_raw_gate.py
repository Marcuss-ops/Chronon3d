#!/usr/bin/env python3
"""Verify SVG dominant-baseline lowering with bundled OpenType BASE data."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "assets" / "fonts" / "FreeSerif.ttf"
WIDTH, HEIGHT = 320, 192
FRAME_BYTES = WIDTH * HEIGHT * 4
BASELINES = (
    "auto", "use-script", "no-change", "reset-size", "alphabetic",
    "central", "middle", "hanging", "ideographic", "mathematical",
    "text-before-edge", "text-after-edge",
)


def render(cli: Path, root: Path, baseline: str) -> bytes:
    stem = f"baseline_{baseline}"
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
      <text x="24" y="104" font-family="FreeSerif" font-size="64" fill="#000000" dominant-baseline="{baseline}">Baseline test</text>
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


def alpha_bbox(frame: bytes) -> tuple[int, int, int, int] | None:
    points = [(i // 4 % WIDTH, i // (4 * WIDTH))
              for i in range(0, len(frame), 4) if frame[i + 3] > 32]
    if not points:
        return None
    return (min(x for x, _ in points), min(y for _, y in points),
            max(x for x, _ in points), max(y for _, y in points))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=os.environ.get("CHRONON_CLI"))
    args = parser.parse_args()
    if not args.cli:
        parser.error("pass --cli or set CHRONON_CLI to an SVG-enabled chronon3d_cli")
    cli = Path(args.cli).resolve()
    if not cli.is_file() or not FONT.is_file():
        parser.error(f"CLI or bundled OpenType BASE test font is missing: {cli}, {FONT}")
    with tempfile.TemporaryDirectory(prefix="svg_text_baseline_") as temp:
        root = Path(temp)
        (root / "fonts").mkdir()
        shutil.copyfile(FONT, root / "fonts" / "FreeSerif-Regular.ttf")
        frames = {baseline: render(cli, root, baseline) for baseline in BASELINES}
        repeat = render(cli, root, "hanging")

    anchor_bbox = alpha_bbox(frames["auto"])
    if anchor_bbox is None:
        raise RuntimeError("auto SVG text baseline produced no visible glyphs")
    inherited_values = {"auto", "use-script", "no-change", "reset-size", "alphabetic"}
    for baseline in BASELINES[1:]:
        bbox = alpha_bbox(frames[baseline])
        if bbox is None:
            raise RuntimeError(f"{baseline} SVG text baseline produced no visible glyphs")
        if baseline in inherited_values and frames[baseline] != frames["auto"]:
            raise RuntimeError(f"{baseline} should retain the script's Roman baseline")
        if baseline in inherited_values:
            continue
        if bbox[1] == anchor_bbox[1] and bbox[3] == anchor_bbox[3]:
            raise RuntimeError(f"{baseline} did not use a distinct vertical baseline: {bbox}")
    if frames["hanging"] != repeat:
        raise RuntimeError("repeated hanging-baseline render differs pixel-for-pixel")
    print("SVG dominant-baseline OpenType/raw placement: PASS")
    for baseline in BASELINES:
        print(f"  {baseline}: bbox={alpha_bbox(frames[baseline])}")
    print("SVG dominant-baseline determinism: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
