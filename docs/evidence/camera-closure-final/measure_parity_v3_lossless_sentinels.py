#!/usr/bin/env python3
"""Measure the encoder-normalized v3 Vulkan/software sentinel comparison."""
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 1920, 1080
THRESHOLDS = {"rgb_mae_max": 2.0, "rgb_p99_max": 10}
ROIS = json.loads((ROOT / "measurement-contract.json").read_text())["roi_xyxy"]


def run(args):
    return subprocess.run(args, check=True, capture_output=True)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    frames = []
    all_pass = True
    for frame in (30, 75, 149):
        software_path = ROOT / f"parity-v3-software-{frame:03}.raw.mp4"
        vulkan_path = ROOT / f"parity-v3-vulkan-{frame:03}-lossless.mp4"
        log_path = ROOT / f"parity-v3-vulkan-{frame:03}-lossless.log"
        software = np.fromfile(software_path, dtype=np.uint8)
        expected = WIDTH * HEIGHT * 4
        if software.size != expected:
            raise ValueError(f"{software_path}: expected {expected} RGBA bytes, got {software.size}")
        software = software.reshape(HEIGHT, WIDTH, 4)[:, :, :3]
        decoded = run([
            "ffmpeg", "-v", "error", "-i", str(vulkan_path), "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
        ]).stdout
        if len(decoded) != WIDTH * HEIGHT * 3:
            raise ValueError(f"{vulkan_path}: decoded frame has {len(decoded)} bytes")
        vulkan = np.frombuffer(decoded, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)
        delta = np.abs(software.astype(np.int16) - vulkan.astype(np.int16)).ravel()
        mae = float(delta.mean())
        p99 = int(np.percentile(delta, 99))
        roi_metrics = {}
        absolute = np.abs(software.astype(np.int16) - vulkan.astype(np.int16))
        for name, (x0, y0, x1, y1) in ROIS.items():
            roi = absolute[y0:y1, x0:x1].ravel()
            roi_metrics[name] = {
                "rgb_mae": float(roi.mean()),
                "rgb_p99": int(np.percentile(roi, 99)),
                "rgb_max": int(roi.max()),
            }
        validation_errors = log_path.read_text(errors="replace").count("Vulkan Validation ERROR")
        passed = mae <= THRESHOLDS["rgb_mae_max"] and p99 <= THRESHOLDS["rgb_p99_max"]
        all_pass &= passed and validation_errors == 0
        frames.append({
            "frame": frame,
            "rgb_mae": mae,
            "rgb_p99": p99,
            "rgb_max": int(delta.max()),
            "preregistered_roi_diagnostics": roi_metrics,
            "software_raw_sha256": sha256(software_path),
            "vulkan_lossless_mp4_sha256": sha256(vulkan_path),
            "vulkan_validation_errors": validation_errors,
            "gate_under_frozen_thresholds": "PASS" if passed else "FAIL",
        })

    report = {
        "plan_sha256": "c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6",
        "comparison": "software RGBA8 raw vs strict-native Vulkan H.264 NVENC lossless preset decoded to RGB24",
        "decoded_vulkan_pixel_format": "yuv420p, BT.709 limited range",
        "thresholds": THRESHOLDS,
        "frames": frames,
        "aggregate_gate": "PASS" if all_pass else "FAIL",
        "note": (
            "Encoder-normalized diagnostic only, not the frozen raw RGBA software/Vulkan gate. "
            "QP0 H.264 still passes through yuv420p conversion; the Vulkan raw sink does not "
            "provide a usable readback for this scene. Do not widen the thresholds."
        ),
    }
    (ROOT / "parity-v3-lossless-sentinels.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
