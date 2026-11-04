#!/usr/bin/env python3
"""Measure frozen Optical Studio focus ROIs on two full raw RGBA8 sequences."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / "measurement-contract.json").read_text())
WIDTH, HEIGHT = 1920, 1080
EXPECTED_FRAMES = 150


def energy(rgb: np.ndarray, roi: list[int]) -> float:
    x0, y0, x1, y1 = roi
    crop = rgb[y0:y1, x0:x1].astype(np.float64)
    gray = crop @ np.array([0.2126, 0.7152, 0.0722])
    return float(np.mean(np.diff(gray, axis=0) ** 2) +
                 np.mean(np.diff(gray, axis=1) ** 2))


def sequence(path: Path) -> np.memmap:
    expected = WIDTH * HEIGHT * 4 * EXPECTED_FRAMES
    if path.stat().st_size != expected:
        raise ValueError(f"{path}: expected {expected} bytes, got {path.stat().st_size}")
    return np.memmap(path, dtype=np.uint8, mode="r",
                     shape=(EXPECTED_FRAMES, HEIGHT, WIDTH, 4))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(8 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dof_raw", type=Path)
    parser.add_argument("sharp_raw", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()

    dof, sharp = sequence(args.dof_raw), sequence(args.sharp_raw)
    expected_focus = {30: "near", 75: "middle", 149: "far"}
    report = {
        "plan_sha256": "c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6",
        "comparison": "DoF ON RGBA8 raw Vulkan readback vs DoF OFF RGBA8 raw Vulkan readback",
        "frames": {},
        "thresholds": {
            "focused_min_ratio": 0.70,
            "defocused_max_ratio": 0.90,
            "source": str(ROOT / "measurement-contract.json"),
        },
        "dof_raw_sha256": sha256(args.dof_raw),
        "sharp_raw_sha256": sha256(args.sharp_raw),
    }

    for frame, focus_name in expected_focus.items():
        dof_rgb = dof[frame, :, :, :3]
        sharp_rgb = sharp[frame, :, :, :3]
        if args.preview_dir:
            args.preview_dir.mkdir(parents=True, exist_ok=True)
            Image.fromarray(dof_rgb).save(args.preview_dir / f"dof-{frame:03}.png")
            Image.fromarray(sharp_rgb).save(args.preview_dir / f"sharp-{frame:03}.png")
        measurements = {}
        for name, roi in CONTRACT["roi_xyxy"].items():
            on = energy(dof_rgb, roi)
            off = energy(sharp_rgb, roi)
            ratio = on / off if off > 0.0 else None
            focused = name == focus_name
            passed = ratio is not None and (
                ratio >= 0.70 if focused else ratio <= 0.90)
            measurements[name] = {
                "dof_edge_energy": on,
                "sharp_edge_energy": off,
                "on_off_ratio": ratio,
                "expected_focused": focused,
                "gate": "PASS" if passed else "FAIL",
            }
        report["frames"][str(frame)] = {
            "expected_focus": focus_name,
            "rois": measurements,
        }

    report["aggregate_gate"] = (
        "PASS" if all(roi["gate"] == "PASS"
                      for frame in report["frames"].values()
                      for roi in frame["rois"].values())
        else "FAIL")
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["aggregate_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
