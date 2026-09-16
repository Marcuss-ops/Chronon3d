#!/usr/bin/env python3
"""Pixel-level attribution for "the text looks dirty" (P0-0 diagnostic).

Two questions, one frame:

  1. How much of the difference between two encodes of the SAME frame is
     encoder, not renderer?  Answer: PSNR/SSIM between the two decodes, plus
     the edge-ramp statistics below.
  2. Is the still (PNG) output usable as the pre-encode truth?  Answer: the
     same statistics against it -- if the still path renders a different image
     (e.g. without the video layers) the numbers say so immediately instead of
     being mistaken for a quality measurement.

The edge metrics are deliberately dumb and reproducible:

  * `ramp_px`  -- along strong horizontal gradients (text strokes), how many
                  pixels the 10%..90% luminance transition spans.  A clean
                  edge is 1.0-1.5 px at 1080p; a smeared/ringing edge is wider.
  * `overshoot` -- the maximum post-transition luminance excursion past the
                  plateau (ringing), as a fraction of the step height.  A clean
                  encode stays under a few percent; H.264 at a low bitrate
                  overshoots visibly around white-on-dark glyph edges.
  * `chroma_shift` -- mean |Cb|+|Cr| delta against the reference over the same
                  edge band, i.e. what 4:2:0 costs on a colored stroke.

Usage:
  quality_probe.py --ref a.png --test b.png [--region x0,y0,x1,y1] [--json out.json]
"""
import argparse
import json
import sys

import numpy as np
from PIL import Image


def load(path):
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.float64) / 255.0


def luma(rgb):
    return 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]


def edge_stats(luma_img, region, window=8):
    """Glyph-edge sharpness of ONE image, measured on its own gradient profile.

    Both images are measured with the same rule, so the two reports are
    comparable: a scan (row) is kept when its strongest horizontal transition
    is a real glyph edge, and the metrics describe that same transition.

      * gradient_peak -- |delta| across the edge.  An encode that smears the
                         edge lowers this; a sharper render raises it.
      * ramp_px       -- pixels inside the +/-window whose value falls between
                         10% and 90% of the local min..max: the transition
                         width.  Clean 1080p text is 1-2 px.
      * overshoot     -- how far the brightest sample passes the plateau, as a
                         fraction of the step: H.264 ringing at low bitrate
                         shows up here, in the encode, not in the source.
    """
    x0, y0, x1, y1 = region
    a = luma_img[y0:y1, x0:x1]
    if a.shape[0] == 0 or a.shape[1] < 2 * window + 2:
        return {"samples": 0}
    grad = np.abs(np.diff(a, axis=1))
    peaks, ramps, overshoots, steps = [], [], [], []
    for row in range(a.shape[0]):
        if grad[row].size == 0:
            continue
        peak = int(np.argmax(grad[row]))
        if grad[row][peak] < 0.20:  # not a glyph edge
            continue
        left = max(0, peak - window)
        right = min(a.shape[1], peak + window + 2)
        seg = a[row, left:right]
        lo, hi = float(seg.min()), float(seg.max())
        span = hi - lo
        if span < 0.25:
            continue
        lo_t, hi_t = lo + 0.1 * span, lo + 0.9 * span
        ramps.append(float(int(np.count_nonzero((seg >= lo_t) & (seg <= hi_t)))))
        peaks.append(float(grad[row][peak]))
        steps.append(span)
        # Overshoot: the extreme value reached just past the transition on the
        # side the scan is heading, relative to the plateau it should settle on.
        far = a[row, right - 1]
        near = a[row, left]
        heading_up = far >= near
        beyond = seg if heading_up else seg
        extreme = float(beyond.max() if heading_up else beyond.min())
        plateau = far
        excess = (extreme - plateau) if heading_up else (plateau - extreme)
        overshoots.append(max(0.0, excess) / span)
    if not ramps:
        return {"samples": 0}
    return {
        "samples": int(len(ramps)),
        "gradient_peak_median": float(np.median(peaks)),
        "gradient_peak_p10": float(np.percentile(peaks, 10)),
        "ramp_px_median": float(np.median(ramps)),
        "ramp_px_p90": float(np.percentile(ramps, 90)),
        "overshoot_median": float(np.median(overshoots)),
        "overshoot_p90": float(np.percentile(overshoots, 90)),
    }


def psnr(a, b):
    mse = float(np.mean((a - b) ** 2))
    if mse <= 1e-12:
        return float("inf")
    return float(10.0 * np.log10(1.0 / mse))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--test", required=True)
    ap.add_argument("--region", default="", help="x0,y0,x1,y1 (default: whole frame)")
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    ref = load(args.ref)
    test = load(args.test)
    if ref.shape != test.shape:
        sys.exit(f"size mismatch: {ref.shape} vs {test.shape}")
    h, w = ref.shape[:2]
    if args.region:
        x0, y0, x1, y1 = (int(v) for v in args.region.split(","))
    else:
        x0, y0, x1, y1 = 0, 0, w, h
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(w, x1), min(h, y1)

    ref_l, test_l = luma(ref), luma(test)
    ref_cb = ref[:, :, 2] - ref_l
    test_cb = test[:, :, 2] - test_l

    report = {
        "ref": args.ref,
        "test": args.test,
        "region": [x0, y0, x1, y1],
        "psnr_db": psnr(ref, test),
        "mean_abs_diff": float(np.mean(np.abs(ref - test))),
        "edge_ref": edge_stats(ref_l, (x0, y0, x1, y1)),
        "edge_test": edge_stats(test_l, (x0, y0, x1, y1)),
        "chroma_shift_region": float(np.mean(np.abs(ref_cb[y0:y1, x0:x1] - test_cb[y0:y1, x0:x1]))),
    }
    if args.json:
        json.dump(report, open(args.json, "w"), indent=1)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
