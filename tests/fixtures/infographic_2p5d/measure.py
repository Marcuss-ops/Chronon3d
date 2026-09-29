#!/usr/bin/env python3
"""Measure what a rendered plan actually contains.

The fixture's claims are about pixels, so they are checked in pixels: decode one
frame of the rendered mp4 and report ink, the bright bounding box and per-colour
bands.  This is deliberately independent of the renderer — it only reads the
encoder's output.

Usage:
    python3 measure.py FILE.mp4[:FRAME] [FILE.mp4[:FRAME] ...]
    python3 measure.py --regions FILE.mp4[:FRAME]

`--regions` also reports ink inside each `20_scene` element's authored box, so
the per-element table in the README is reproducible rather than descriptive.
"""

from __future__ import annotations

import io
import subprocess
import sys

import numpy as np
from PIL import Image

# A pixel counts as "ink" when its brightest channel clears this.  Everything the
# fixtures draw is far brighter than the gradients they sit on.
THRESHOLD = 60

# Authored boxes for the 20_scene elements, in canvas pixels (y down).  Derived
# from the constants in scene_infographic.py: 2D layers are placed at the canvas
# centre plus their authored offset, so e.g. the funnel group at `at(170, -20)`
# is centred on canvas (650, 250).  The boxes are generous on purpose — they
# answer "did this element draw at all", and neighbours may overlap them.
REGIONS = {
    "title + subtitle": (0, 20, 960, 130),
    "tilted 3D disc":   (620, 45, 940, 115),
    "funnel stages":    (470, 160, 830, 340),
    "counters":         (350, 160, 590, 340),
    "step chart":       (560, 150, 910, 320),
    "ribbon":           (180, 320, 780, 500),
    "badges":           (90, 470, 870, 530),
}


def decode_frame(path: str, index: int) -> np.ndarray:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-vf", "select=eq(n\\,%d)" % index,
         "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"],
        capture_output=True, check=True).stdout
    return np.asarray(Image.open(io.BytesIO(raw)).convert("RGB")).astype(np.int16)


def bands(rgb: np.ndarray) -> dict:
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    return {
        "white": (r > 180) & (g > 180) & (b > 180),
        "warm": (r > 140) & (r - b > 60) & (r - g > 30),
        "cool": (b > 120) & (b - r > 40),
        "teal": (g > 120) & (g - r > 40),
    }


def bbox_of(mask: np.ndarray):
    if not mask.any():
        return None
    ys, xs = np.nonzero(mask)
    return (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))


def report(path: str, index: int) -> dict:
    rgb = decode_frame(path, index)
    height, width, _ = rgb.shape
    ink = rgb.max(axis=2) > THRESHOLD
    print("%s  frame=%d  %dx%d" % (path, index, width, height))
    print("  ink(lum>%d) = %d px  bbox = %s" % (THRESHOLD, int(ink.sum()), bbox_of(ink)))
    found = {}
    for name, mask in bands(rgb).items():
        count = int(mask.sum())
        print("  %-5s = %7d px  bbox = %s" % (name, count, bbox_of(mask)))
        found[name] = (count, bbox_of(mask))
    return {"ink": (int(ink.sum()), bbox_of(ink)), "bands": found}


def report_regions(path: str, index: int) -> None:
    rgb = decode_frame(path, index)
    ink = rgb.max(axis=2) > THRESHOLD
    print("  element regions (ink px inside the authored box):")
    for name, (x0, y0, x1, y1) in REGIONS.items():
        count = int(ink[y0:y1, x0:x1].sum())
        verdict = "ok" if count else "EMPTY"
        print("    %-18s %7d px  box=(%d,%d,%d,%d)  %s"
              % (name, count, x0, y0, x1, y1, verdict))


def main() -> int:
    args = sys.argv[1:]
    show_regions = "--regions" in args
    args = [a for a in args if a != "--regions"]
    if not args:
        print(__doc__)
        return 2
    for arg in args:
        path, _, index = arg.rpartition(":")
        if not path:
            path, index = index, "42"
        print()
        report(path, int(index))
        if show_regions:
            report_regions(path, int(index))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
