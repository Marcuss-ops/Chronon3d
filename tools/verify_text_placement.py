#!/usr/bin/env python3
"""
verify_text_placement.py — Numerical analysis of text placement PNGs.

Checks per image:
  - max_luma >= threshold (text is visible)
  - alpha_area >= min_pixels (enough visible content)
  - alpha_centroid near expected center (text is centered)
  - bbox not touching framebuffer edges (no clipping)

Usage:
  python3 tools/verify_text_placement.py <directory_of_pngs>
  python3 tools/verify_text_placement.py test_renders/golden/text/placement/

Exit code: 0 if all PASS, 1 if any FAIL.
"""

import sys
import os
import glob
import json

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow is required. Install with: pip3 install Pillow")
    sys.exit(2)


def analyze_png(path: str) -> dict:
    """Analyze a single PNG for text placement quality metrics."""
    img = Image.open(path).convert("RGBA")
    w, h = img.size
    pixels = img.load()

    # Collect alpha and luma stats
    max_luma = 0
    alpha_sum_x = 0.0
    alpha_sum_y = 0.0
    alpha_sum_w = 0.0
    alpha_area = 0
    max_alpha = 0.0

    # Bounding box of visible content (alpha > threshold)
    alpha_threshold = 0.05
    bbox_x0, bbox_y0 = w, h
    bbox_x1, bbox_y1 = 0, 0

    # Edge touch flags
    touches_top = False
    touches_bot = False
    touches_left = False
    touches_right = False

    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            a_norm = a / 255.0
            luma = int(0.299 * r + 0.587 * g + 0.114 * b)

            if luma > max_luma:
                max_luma = luma
            if a_norm > max_alpha:
                max_alpha = a_norm

            if a_norm > alpha_threshold:
                alpha_sum_x += x * a_norm
                alpha_sum_y += y * a_norm
                alpha_sum_w += a_norm
                alpha_area += 1

                if x < bbox_x0: bbox_x0 = x
                if x > bbox_x1: bbox_x1 = x
                if y < bbox_y0: bbox_y0 = y
                if y > bbox_y1: bbox_y1 = y

                if y == 0:      touches_top = True
                if y == h - 1:  touches_bot = True
                if x == 0:      touches_left = True
                if x == w - 1:  touches_right = True

    # Compute centroid
    if alpha_sum_w > 0:
        centroid_x = alpha_sum_x / alpha_sum_w
        centroid_y = alpha_sum_y / alpha_sum_w
    else:
        centroid_x = -1.0
        centroid_y = -1.0

    expected_cx = w / 2.0
    expected_cy = h / 2.0
    delta_x = centroid_x - expected_cx
    delta_y = centroid_y - expected_cy
    delta_dist = (delta_x**2 + delta_y**2) ** 0.5

    clipped = touches_top or touches_bot or touches_left or touches_right

    # Determine PASS/FAIL
    fail_reasons = []
    if max_luma < 200:
        fail_reasons.append(f"max_luma={max_luma} < 200")
    if alpha_area < 100:
        fail_reasons.append(f"alpha_area={alpha_area} < 100")
    # Centroid tolerance: 10% of width
    tolerance = w * 0.10
    if delta_dist > tolerance and centroid_x >= 0:
        fail_reasons.append(f"centroid off-center by {delta_dist:.0f}px (tol={tolerance:.0f})")
    if clipped:
        fail_reasons.append("bbox touches edge (possible clipping)")

    passed = len(fail_reasons) == 0

    return {
        "path": path,
        "size": f"{w}x{h}",
        "max_luma": max_luma,
        "max_alpha": round(max_alpha, 3),
        "alpha_area": alpha_area,
        "centroid": (round(centroid_x, 1), round(centroid_y, 1)),
        "expected_center": (round(expected_cx, 1), round(expected_cy, 1)),
        "delta": (round(delta_x, 1), round(delta_y, 1)),
        "delta_dist": round(delta_dist, 1),
        "clipped": clipped,
        "bbox": (bbox_x0, bbox_y0, bbox_x1, bbox_y1) if alpha_area > 0 else None,
        "passed": passed,
        "fail_reasons": fail_reasons,
    }


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 verify_text_placement.py <directory_of_pngs>")
        print("       python3 verify_text_placement.py test_renders/golden/text/placement/")
        sys.exit(2)

    directory = sys.argv[1]
    if not os.path.isdir(directory):
        print(f"ERROR: {directory} is not a directory")
        sys.exit(2)

    png_files = sorted(glob.glob(os.path.join(directory, "*.png")))
    if not png_files:
        print(f"ERROR: No PNG files found in {directory}")
        sys.exit(2)

    # Optional JSON output
    json_mode = "--json" in sys.argv

    results = []
    for png_path in png_files:
        result = analyze_png(png_path)
        results.append(result)

    if json_mode:
        print(json.dumps(results, indent=2))
    else:
        # Print table header
        name_width = max(len(os.path.basename(r["path"])) for r in results)
        print(f"{'Name':<{name_width}}  {'Status':>6}  {'Center':>14}  {'Delta':>10}  {'Max':>3}  {'Area':>6}  {'Clip':>4}")
        print("-" * (name_width + 56))

        for r in results:
            name = os.path.basename(r["path"])
            status = "PASS" if r["passed"] else "FAIL"
            center = f"({r['centroid'][0]:.0f},{r['centroid'][1]:.0f})"
            delta = f"({r['delta'][0]:+.0f},{r['delta'][1]:+.0f})"
            clipped = "yes" if r["clipped"] else "no"
            print(f"{name:<{name_width}}  {status:>6}  {center:>14}  {delta:>10}  {r['max_luma']:>3}  {r['alpha_area']:>6}  {clipped:>4}")

        # Print failures
        failures = [r for r in results if not r["passed"]]
        print()
        if failures:
            print(f"FAILURES ({len(failures)}/{len(results)}):")
            for r in failures:
                print(f"  {os.path.basename(r['path'])}: {', '.join(r['fail_reasons'])}")
        else:
            print(f"ALL {len(results)} PASSED")

    sys.exit(0 if all(r["passed"] for r in results) else 1)


if __name__ == "__main__":
    main()
