#!/usr/bin/env python3
"""End-to-end gate for the documentary showcase (TICKET-CAMERA-DEFINITIVE-CLOSURE).

Decodes frames from the ENCODED video (never the PNG lane, which does not compose
text) and asserts the visible content of each stage. Fails closed.

Usage: e2e_documentary_gate.py <documentary_showcase.mp4>
"""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 1920, 1080
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)


def frame(video, n, tmp):
    out = Path(tmp) / f"f{n}.png"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", video,
                    "-vf", f"select=eq(n\\,{n})", "-vframes", "1", str(out)], check=True)
    if not out.exists():
        failures.append(f"frame {n} could not be decoded")
        return None
    return np.asarray(Image.open(out).convert("RGB")).astype(np.int32)


def frac(mask):
    return float(mask.mean())


def main():
    video = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        f12, f40, f100 = (frame(video, n, tmp) for n in (12, 40, 100))
        if f12 is None or f40 is None or f100 is None:
            return 1
        for name, f in (("12", f12), ("40", f40), ("100", f100)):
            r, g, b = f[..., 0], f[..., 1], f[..., 2]
            check(f.shape == (H, W, 3), f"frame {name}: size {W}x{H}")
            check(frac((r < 20) & (g > 200) & (b < 20)) < 0.01,
                  f"frame {name}: no pure-green (broken YUV plane) region")
            check(frac((r + g + b) < 45) < 0.5, f"frame {name}: not mostly black")

        # Stage 1: warm paper left/right halves must agree (catches the pink|green split).
        left, right = f12[:, : W // 2].mean(axis=(0, 1)), f12[:, W // 2:].mean(axis=(0, 1))
        check(np.abs(left - right).max() < 25, "frame 12: left/right halves match (no plane split)")
        check(left[0] > left[2] and left.min() > 150, "frame 12: bright warm paper")

        # Stage 1: serif text (dark) and cyan Multiply highlighter bars.
        r, g, b = f40[..., 0], f40[..., 1], f40[..., 2]
        cyan = (b > 200) & (g > 120) & (g < 200) & (r < 130)
        check(frac(cyan) > 0.03, "frame 40: cyan highlighter visible (Multiply bars)")
        check(frac((r < 60) & (g < 60) & (b < 70)) > 0.001, "frame 40: dark serif text present")

        # Stage 2/3: dark subject on the right, pop-up badges on the left.
        r, g, b = f100[..., 0], f100[..., 1], f100[..., 2]
        right_dark = (r + g + b)[:, int(W * 0.5):] < 200
        check(frac(right_dark) > 0.15, "frame 100: subject cutout present on the right")
        badge_blue = (b > 200) & (r < 80) & (g > 120)
        badge_green = (g > 160) & (r < 70) & (b < 130)
        check(frac(badge_blue[:, : W // 2]) > 0.005, "frame 100: logo badge visible")
        check(frac(badge_green[:, : W // 2]) > 0.005, "frame 100: $10,000 tag visible")

        # Stage 2: DoF rack focus makes the background document softer than at frame 40.
        def sharp(f, box):
            y0, y1, x0, x1 = box
            gray = f[y0:y1, x0:x1].sum(axis=2)
            return float(np.abs(np.diff(gray, axis=1)).mean())
        check(sharp(f100, (350, 780, 330, 1200)) < sharp(f40, (350, 780, 330, 1200)),
              "frame 100: background document defocused vs frame 40")

        # Stage 4: vignette darkens the corners relative to the centre.
        for name, f in (("40", f40),):
            corner = f[:80, :80].mean()
            centre = f[H // 2 - 40: H // 2 + 40, W // 2 - 40: W // 2 + 40].mean()
            check(corner < centre, f"frame {name}: vignette (corner darker than centre)")

    print("\nRESULT:", "FAIL (%d)" % len(failures) if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
