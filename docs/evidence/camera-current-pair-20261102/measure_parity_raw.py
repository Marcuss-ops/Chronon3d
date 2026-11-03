#!/usr/bin/env python3
"""Software vs Vulkan raw RGBA8 parity for the camera acceptance pair.

Re-pin of `camera-closure-final/measure_parity_v3_raw_sentinels.py` on the
current Chronon3D + ChrononMotion3D revisions.  The oracle is NOT changed:
thresholds (MAE <= 2.0, p99 <= 10), ROIs and sentinel frames come from
`measurement-contract.json`, copied verbatim from `camera-closure-final/`.

The plan digest is computed from the plan file instead of being hardcoded, so a
regenerated plan cannot silently ride on the old PASS.
"""
import hashlib
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
PLAN = REPO / "docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json"
CONTRACT = json.loads((ROOT / "measurement-contract.json").read_text())
WIDTH, HEIGHT = 1920, 1080
THRESHOLDS = {"rgb_mae_max": 2.0, "rgb_p99_max": 10}
ROIS = CONTRACT["roi_xyxy"]
FRAMES = CONTRACT["frames"]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    expected = WIDTH * HEIGHT * 4
    frames = []
    all_pass = True

    for frame in FRAMES:
        software_path = ROOT / f"parity-v3-software-{frame:03}.raw.mp4"
        vulkan_path = ROOT / f"parity-v3-vulkan-{frame:03}.raw.rgba"
        log_path = ROOT / f"parity-v3-vulkan-{frame:03}-raw.log"

        software_bytes = np.fromfile(software_path, dtype=np.uint8)
        vulkan_bytes = np.fromfile(vulkan_path, dtype=np.uint8)
        for path, data in ((software_path, software_bytes), (vulkan_path, vulkan_bytes)):
            if data.size != expected:
                raise SystemExit(f"{path}: expected {expected} RGBA bytes, got {data.size}")

        software = software_bytes.reshape(HEIGHT, WIDTH, 4)[:, :, :3]
        vulkan = vulkan_bytes.reshape(HEIGHT, WIDTH, 4)[:, :, :3]

        absolute = np.abs(software.astype(np.int16) - vulkan.astype(np.int16))
        delta = absolute.ravel()
        mae = float(delta.mean())
        p99 = int(np.percentile(delta, 99))
        max_d = int(delta.max())

        roi_metrics = {}
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
            "rgb_max": max_d,
            "preregistered_roi_diagnostics": roi_metrics,
            "software_raw_sha256": sha256(software_path),
            "vulkan_raw_sha256": sha256(vulkan_path),
            "vulkan_validation_errors": validation_errors,
            "gate_under_frozen_thresholds": "PASS" if passed else "FAIL",
        })

    report = {
        "plan": str(PLAN.relative_to(REPO)),
        "plan_sha256": sha256(PLAN),
        "comparison": "software RGBA8 raw vs Vulkan RGBA8 raw readback",
        "thresholds": THRESHOLDS,
        "frames": frames,
        "aggregate_gate": "PASS" if all_pass else "FAIL",
    }
    (ROOT / "parity-raw-sentinels.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
