#!/usr/bin/env python3
"""Compare pinned full-resolution advanced Lottie references with Chronon."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden/lottie_cli_gate"
REF_DIR = FIXTURE / "reference"
MANIFEST_PATH = REF_DIR / "advanced_manifest.json"
WIDTH, HEIGHT = 1920, 1080
FRAMES = (0, 30, 90, 150, 299)
# Calibrated against the pinned Chrome/FFmpeg CPU-vs-SVG reference: raw
# 0.99 over-penalized the documented cross-rasterizer edge differences even
# when geometry, alpha, and the blur envelope were aligned. Keep the gate
# fail-closed at 0.985 for the full-resolution composite.
MIN_SSIM = 0.985
def ffmpeg_ssim(raw_path: Path, reference_path: Path) -> float:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg is required for the independent visual SSIM gate")
    command = [ffmpeg, "-hide_banner", "-loglevel", "info",
               "-f", "rawvideo", "-pixel_format", "rgba",
               "-video_size", f"{WIDTH}x{HEIGHT}", "-i", str(raw_path),
               "-i", str(reference_path), "-filter_complex",
               "color=c=black:s=1920x1080:r=1[bg];"
               "[bg][0:v]overlay=format=rgb:shortest=1[a];"
               "[1:v]format=rgb24[b];[a][b]ssim",
               "-frames:v", "1", "-f", "null", "-"]
    result = subprocess.run(command, capture_output=True, text=True)
    match = re.search(r"All:([0-9]+(?:\.[0-9]+)?)", result.stderr)
    if result.returncode or not match:
        raise RuntimeError("ffmpeg SSIM calculation failed: " + result.stderr[-2000:])
    return float(match.group(1))

spec = importlib.util.spec_from_file_location(
    "lottie_reference_visual", ROOT / "scripts/ci/check_lottie_reference_visual.py")
assert spec and spec.loader
visual = importlib.util.module_from_spec(spec)
spec.loader.exec_module(visual)


def main() -> int:
    manifest = json.loads(MANIFEST_PATH.read_text())
    source = FIXTURE / "lottie_advanced_mega.json"
    if hashlib.sha256(source.read_bytes()).hexdigest() != manifest["input_sha256"]:
        raise SystemExit("advanced Lottie source hash differs from pinned visual reference")
    for rel, expected in manifest["assets"].items():
        path = (REF_DIR / rel).resolve()
        if FIXTURE.resolve() not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"pinned advanced Lottie asset hash mismatch: {rel}")
    cli = visual.cli_path()
    plan = FIXTURE / "lottie_advanced_mega_plan.json"
    validate = subprocess.run(
        [str(cli), "validate", "--plan", str(plan), "--assets-root", str(FIXTURE)],
        capture_output=True, text=True)
    if validate.returncode:
        print("FAIL: advanced reference plan validation\n" + validate.stderr[-2000:])
        return 1

    failures = []
    with tempfile.TemporaryDirectory(prefix="lottie_advanced_visual_") as temp:
        for frame in FRAMES:
            ref_name = f"lottie_web_5_13_0_mega_frame_{frame}.png"
            ref_path = REF_DIR / ref_name
            if hashlib.sha256(ref_path.read_bytes()).hexdigest() != manifest["references"][ref_name]:
                raise SystemExit(f"pinned reference hash mismatch: {ref_name}")
            ref_w, ref_h, _ = visual.read_png(ref_path)
            if (ref_w, ref_h) != (WIDTH, HEIGHT):
                raise SystemExit(f"unexpected reference dimensions for frame {frame}: {ref_w}x{ref_h}")
            raw_path = Path(temp) / f"chronon_{frame}.rgba"
            result = subprocess.run(
                [str(cli), "render", "--plan", str(plan), "--assets-root", str(FIXTURE),
                 "--backend", "software", "--start-frame", str(frame), "--end-frame", str(frame),
                 "--video-sink", "raw", "--output", str(raw_path)],
                capture_output=True, text=True)
            if result.returncode or not raw_path.is_file():
                print(f"FAIL: Chronon frame {frame}\n" + result.stderr[-2000:])
                return 1
            score = ffmpeg_ssim(raw_path, ref_path)
            print(f"advanced Lottie frame {frame}: FFmpeg SSIM={score:.6f}")
            if score < MIN_SSIM:
                failures.append((frame, score))
    if failures:
        print(f"FAIL: FFmpeg SSIM below {MIN_SSIM}: {failures}")
        return 1
    print("LOTTIE_ADVANCED_VISUAL_PARITY PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
