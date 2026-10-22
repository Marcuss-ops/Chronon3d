#!/usr/bin/env python3
"""Measure the frame-75 T16 shutter A/B and decoded-backend parity."""
from pathlib import Path
import json
import numpy as np

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 1920, 1080
PIXELS = WIDTH * HEIGHT


def rgb_metric(a: np.ndarray, b: np.ndarray) -> dict[str, float]:
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return {
        "mae": float(diff.mean()),
        "p99_absolute_error": float(np.percentile(diff, 99)),
        "max_absolute_error": int(diff.max()),
        "changed_channel_percent": float(np.mean(diff != 0) * 100.0),
        "channels_error_over_5_percent": float(np.mean(diff > 5) * 100.0),
    }


def rgba_rgb_frame(name: str, frame_index: int) -> np.ndarray:
    frames = np.fromfile(ROOT / name, dtype=np.uint8).reshape(
        9, HEIGHT, WIDTH, 4
    )
    return frames[frame_index, :, :, :3]


def decoded_rgb(name: str) -> np.ndarray:
    return np.fromfile(ROOT / name, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)


software_shutter = rgba_rgb_frame("software-9f-shutter.rgba", 4)
software_baseline = rgba_rgb_frame("software-9f-baseline.rgba", 4)
gpu_shutter = decoded_rgb("vulkan-center75-shutter-decoded.rgb")
gpu_baseline = decoded_rgb("vulkan-center75-no-shutter-decoded.rgb")

result = {
    "scope": "T16 frame 75 only; 1920x1080; same three-depth RenderPlan assets and camera sample",
    "software_reference": "9-frame software Vulkan-independent raw RGBA8; center output frame index 4",
    "gpu_path": "strict require_gpu_native_full_graph, Vulkan RTX A4000, H.264 NVENC; decoded RGB24",
    "shutter": {"angle_degrees": 90, "samples": 8},
    "pixel_gate": {"mae_max": 2.0, "p99_absolute_error_max": 5.0},
    "gpu_shutter_vs_software_shutter": rgb_metric(gpu_shutter, software_shutter),
    "gpu_shutter_vs_gpu_no_shutter": rgb_metric(gpu_shutter, gpu_baseline),
    "software_shutter_vs_software_no_shutter": rgb_metric(
        software_shutter, software_baseline
    ),
}
result["pixel_gate"]["status"] = (
    "PASS"
    if result["gpu_shutter_vs_software_shutter"]["mae"] <= 2.0
    and result["gpu_shutter_vs_software_shutter"]["p99_absolute_error"] <= 5.0
    else "FAIL"
)
(ROOT / "metrics.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
if result["pixel_gate"]["status"] != "PASS":
    raise SystemExit(1)
