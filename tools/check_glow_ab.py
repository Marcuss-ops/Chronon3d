#!/usr/bin/env python3
"""
check_glow_ab.py — Glow A/B acceptance script.

Two sub-commands:

  python3 tools/check_glow_ab.py luma <with_glow.png> <no_glow.png>
    → Measures core preservation (≥98%), halo gain (≥1.0 luma),
      background invariance (delta ≤1.0).  Exit 0 = PASS.

  python3 tools/check_glow_ab.py bbox <with_glow.png> <no_glow.png>
    → Verifies the glow alpha bbox is ≥ the source alpha bbox in
      both dimensions AND strictly larger in at least one.
      Exit 0 = PASS.

TICKET-GLOW-CERTIFICATION — Azione 1 (atomic chore commit on main).
"""

from pathlib import Path
from PIL import Image, ImageFilter
import sys


# ── Luminance helpers ──────────────────────────────────────────────────────

def luminance(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def mean_luma(image: Image.Image, mask: Image.Image) -> float:
    pixels = image.convert("RGB")
    values: list[float] = []
    for rgb, enabled in zip(pixels.getdata(), mask.getdata()):
        if enabled:
            values.append(luminance(rgb))
    if not values:
        raise RuntimeError("Empty mask — no visible pixels found")
    return sum(values) / len(values)


def visible_bbox(path: str, threshold: int = 3) -> tuple[int, int, int, int]:
    """Return (x0, y0, x1, y1) of pixels with alpha > threshold."""
    image = Image.open(path).convert("RGBA")
    width, height = image.size
    xs: list[int] = []
    ys: list[int] = []
    for y in range(height):
        for x in range(width):
            if image.getpixel((x, y))[3] > threshold:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise RuntimeError(f"No visible pixels in {path}")
    return min(xs), min(ys), max(xs), max(ys)


# ── Sub-command: luma ─────────────────────────────────────────────────────

def cmd_luma(with_path: str, without_path: str) -> int:
    with_glow = Image.open(with_path).convert("RGBA")
    without_glow = Image.open(without_path).convert("RGBA")

    if with_glow.size != without_glow.size:
        raise RuntimeError("Images have different dimensions")

    # Core mask: pixels visible in the no-glow (source) image.
    core = Image.new("L", without_glow.size)
    core_pixels = []
    for r, g, b, a in without_glow.getdata():
        lum = luminance((r, g, b))
        core_pixels.append(255 if a > 8 and lum > 20 else 0)
    core.putdata(core_pixels)

    # Halo: outer band around core, ~35 px wide.
    outer = core.filter(ImageFilter.MaxFilter(71))
    inner = core.filter(ImageFilter.MaxFilter(7))
    halo = Image.new("L", core.size)
    halo.putdata([
        255 if outer_val and not inner_val else 0
        for outer_val, inner_val in zip(outer.getdata(), inner.getdata())
    ])

    # Far background: everything outside ~70 px from core.
    far_outer = core.filter(ImageFilter.MaxFilter(141))
    background = Image.new("L", core.size)
    background.putdata([
        0 if val else 255
        for val in far_outer.getdata()
    ])

    core_with = mean_luma(with_glow, core)
    core_without = mean_luma(without_glow, core)
    halo_with = mean_luma(with_glow, halo)
    halo_without = mean_luma(without_glow, halo)
    background_with = mean_luma(with_glow, background)
    background_without = mean_luma(without_glow, background)

    core_ratio = core_with / max(core_without, 0.001)
    halo_gain = halo_with - halo_without
    background_delta = abs(background_with - background_without)

    print({
        "core_with": round(core_with, 4),
        "core_without": round(core_without, 4),
        "core_ratio": round(core_ratio, 4),
        "halo_with": round(halo_with, 4),
        "halo_without": round(halo_without, 4),
        "halo_gain": round(halo_gain, 4),
        "background_delta": round(background_delta, 4),
    })

    errors = []
    if core_ratio < 0.98:
        errors.append(f"FAIL: glow darkens text, ratio={core_ratio:.4f} (need >= 0.98)")
    if halo_gain < 1.0:
        errors.append(f"FAIL: halo not clearly measurable, gain={halo_gain:.4f} (need >= 1.0)")
    if background_delta > 1.0:
        errors.append(f"FAIL: glow alters far background, delta={background_delta:.4f} (need <= 1.0)")

    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        return 1

    print("PASS: source preserved, halo present, background unchanged")
    return 0


# ── Sub-command: bbox ─────────────────────────────────────────────────────

def cmd_bbox(with_path: str, without_path: str) -> int:
    source = visible_bbox(without_path)
    glow = visible_bbox(with_path)

    source_width = source[2] - source[0] + 1
    source_height = source[3] - source[1] + 1
    glow_width = glow[2] - glow[0] + 1
    glow_height = glow[3] - glow[1] + 1

    print(f"source bbox: {source}  ({source_width}×{source_height})")
    print(f"glow   bbox: {glow}    ({glow_width}×{glow_height})")

    if glow_width < source_width:
        print(f"FAIL: glow bbox width ({glow_width}) < source ({source_width})", file=sys.stderr)
        return 1
    if glow_height < source_height:
        print(f"FAIL: glow bbox height ({glow_height}) < source ({source_height})", file=sys.stderr)
        return 1
    if not (glow_width > source_width or glow_height > source_height):
        print("FAIL: glow bbox is not larger than source in any dimension", file=sys.stderr)
        return 1

    print("PASS: glow generates a real external halo")
    return 0


# ── Main ───────────────────────────────────────────────────────────────────

def main() -> int:
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} luma|bbox <with_glow.png> <no_glow.png>", file=sys.stderr)
        return 2

    cmd = sys.argv[1]
    with_path = sys.argv[2]
    without_path = sys.argv[3]

    for p in (with_path, without_path):
        if not Path(p).is_file():
            print(f"ERROR: file not found: {p}", file=sys.stderr)
            return 2

    if cmd == "luma":
        return cmd_luma(with_path, without_path)
    elif cmd == "bbox":
        return cmd_bbox(with_path, without_path)
    else:
        print(f"ERROR: unknown sub-command '{cmd}'.  Use 'luma' or 'bbox'.", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
