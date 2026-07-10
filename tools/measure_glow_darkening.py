#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════════════════
# tools/measure_glow_darkening.py — BUG 2 / TICKET-TEXT-GLOW-DARKENING A/B test
# (Step 3 of the ticket; refs docs/baselines/2026-07-10-glow-ab-result.md).
#
# Compares two PNGs (the WITH-glow and WITHOUT-glow render of the same
# AnimTypewriterGlow scene) and reports the mean luminance (Rec.709) of a
# fixed bounding box (the text ink region).  If the WITH-glow case has
# materially LOWER mean luminance than the WITHOUT-glow case, the
# "Glow darkens" claim is confirmed (FAIL); otherwise it is excluded
# (PASS — the claim is not supported by the experiment).
#
# Pure stdlib + Pillow (no numpy).  Deterministic, no side effects beyond
# a single stdout line + a non-zero exit on FAIL.
#
# CLI:
#   python3 tools/measure_glow_darkening.py <with_png> <without_png>
#       [--bbox x0,y0,x1,y1]              # default: 460,280,1460,800 (text ink region)
#       [--threshold-pct 2.0]             # default: 2.0% delta-luminance
#       [--frames N path1.png path2.png ...]   # multi-frame average (optional)
#
# Exit codes:
#   0 — PASS (darkening EXCLUDED: |delta_pct| < threshold)
#   1 — FAIL (darkening CONFIRMED: with-glow is darker by >= threshold)
#   2 — input error (size mismatch, missing file, decode error)
#   3 — internal error (unexpected)
#
# Output: a single human-readable line (and an optional multi-frame
# table when --frames is given) on stdout; nothing on stderr unless an
# error occurs.
# ═══════════════════════════════════════════════════════════════════════════

import argparse
import os
import sys

try:
    from PIL import Image  # Pillow >= 10
except ImportError:
    print("ERR|pillow_missing|install: pip3 install Pillow", file=sys.stderr)
    sys.exit(2)


# Rec.709 luminance weights (HDTV standard; the same formula used by
# the C++ image_diff.hpp in tests/visual/support/).
_LUM_R = 0.2126
_LUM_G = 0.7152
_LUM_B = 0.0722


def _load_rgb(path: str) -> Image.Image:
    """Decode a PNG and convert to RGB.  Raises OSError on missing/decode error."""
    return Image.open(path).convert("RGB")


def _mean_luminance_bbox(img: Image.Image, bbox: tuple[int, int, int, int]) -> float:
    """Mean Rec.709 luminance over the bbox (x0, y0, x1, y1) of an RGB image.
    Returns 0..255."""
    x0, y0, x1, y1 = bbox
    # Pillow's getdata(band) yields per-pixel band values; slicing with
    # .crop first then computing bytes is faster for large images.
    region = img.crop((x0, y0, x1, y1))
    width, height = region.size
    n = width * height
    # Vectorized via bytes() for speed (single allocation, C-level loop in Pillow).
    r_bytes = region.tobytes("raw", "R")
    g_bytes = region.tobytes("raw", "G")
    b_bytes = region.tobytes("raw", "B")
    # Sum of (wr*r + wg*g + wb*b) for all pixels — 8-bit values, 0..255.
    # Use sum() on bytes (Python fast path) — same cost as numpy for n < 1M pixels.
    total = (
        sum(b * _LUM_R for b in r_bytes)
        + sum(b * _LUM_G for b in g_bytes)
        + sum(b * _LUM_B for b in b_bytes)
    )
    return total / n


def _check_size_match(img_a: Image.Image, img_b: Image.Image) -> None:
    if img_a.size != img_b.size:
        raise ValueError(
            f"size_mismatch: a={list(img_a.size)} b={list(img_b.size)}"
        )


def _measure_pair(with_path: str, without_path: str, bbox: tuple[int, int, int, int]) -> dict:
    """Compute mean luminance for both PNGs in the bbox.  Returns dict."""
    img_with = _load_rgb(with_path)
    img_without = _load_rgb(without_path)
    _check_size_match(img_with, img_without)
    lum_with = _mean_luminance_bbox(img_with, bbox)
    lum_without = _mean_luminance_bbox(img_without, bbox)
    # Sign convention: positive delta = with-glow is BRIGHTER (excludes darkening).
    # Negative delta = with-glow is DARKER (confirms darkening).
    delta_abs = lum_with - lum_without
    if lum_without > 0.0:
        delta_pct = (delta_abs / lum_without) * 100.0
    else:
        delta_pct = 0.0  # degenerate (all-zero bbox) — undefined, default to PASS
    return {
        "with_path": with_path,
        "without_path": without_path,
        "bbox": bbox,
        "lum_with": lum_with,
        "lum_without": lum_without,
        "delta_abs": delta_abs,
        "delta_pct": delta_pct,
    }


def _verdict(delta_pct: float, threshold_pct: float) -> tuple[str, str]:
    """Return (verdict, reason).  Darkening = with-glow is DARKER (negative delta)."""
    # |delta_pct| < threshold  → PASS (excluded: any sign; the experiment is
    # consistent with "no darkening").
    # delta_pct <= -threshold  → FAIL (confirmed: with-glow darker by >= threshold).
    # delta_pct > +threshold   → PASS (excluded — opposite effect, no darkening).
    if delta_pct <= -threshold_pct:
        return "FAIL", f"with-glow darker by {-delta_pct:.3f}% >= {threshold_pct}%"
    return "PASS", f"|delta|={abs(delta_pct):.3f}% < {threshold_pct}%"


def _format_single(result: dict, threshold_pct: float) -> str:
    verdict, reason = _verdict(result["delta_pct"], threshold_pct)
    return (
        f"OK|{os.path.basename(result['with_path'])}|"
        f"lum_with={result['lum_with']:.4f}|"
        f"lum_without={result['lum_without']:.4f}|"
        f"delta_abs={result['delta_abs']:+.4f}|"
        f"delta_pct={result['delta_pct']:+.4f}%|"
        f"threshold={threshold_pct}%|"
        f"bbox={result['bbox']}|"
        f"verdict={verdict}|"
        f"reason={reason}"
    )


def _parse_bbox(s: str) -> tuple[int, int, int, int]:
    parts = s.split(",")
    if len(parts) != 4:
        raise argparse.ArgumentTypeError(
            f"bbox must be 'x0,y0,x1,y1' (4 ints), got: {s}"
        )
    try:
        return tuple(int(p) for p in parts)  # type: ignore[return-value]
    except ValueError as ex:
        raise argparse.ArgumentTypeError(f"bbox non-integer: {ex}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Glow A/B test luminance comparator (BUG 2 / TICKET-TEXT-GLOW-DARKENING).",
    )
    ap.add_argument("with_png", help="PNG rendered with glow_intensity=0.5 (the 'with' case)")
    ap.add_argument("without_png", help="PNG rendered with glow_intensity=0.0 (the 'without' case)")
    ap.add_argument(
        "--bbox",
        type=_parse_bbox,
        default=(460, 280, 1460, 800),
        help="Bounding box (x0,y0,x1,y1) in PNG pixel coords. Default: 460,280,1460,800 "
             "(text ink region of the 1920x1080 AnimTypewriterGlow frame, "
             "covering both 88pt/104pt lines with margin).",
    )
    ap.add_argument(
        "--threshold-pct",
        type=float,
        default=2.0,
        help="Darkening threshold in percent. If with-glow is darker by >= this, FAIL. "
             "Default: 2.0 (matches the conservative threshold used in ae_parity_referee).",
    )
    args = ap.parse_args()

    try:
        result = _measure_pair(args.with_png, args.without_png, args.bbox)
    except FileNotFoundError as ex:
        print(f"ERR|file_missing|{ex}", file=sys.stderr)
        return 2
    except ValueError as ex:
        print(f"ERR|{ex}", file=sys.stderr)
        return 2
    except Exception as ex:  # pragma: no cover — defensive
        print(f"ERR|internal|{ex}", file=sys.stderr)
        return 3

    print(_format_single(result, args.threshold_pct))

    verdict, _reason = _verdict(result["delta_pct"], args.threshold_pct)
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
