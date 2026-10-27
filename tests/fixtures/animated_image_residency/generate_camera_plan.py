#!/usr/bin/env python3
"""Generate the camera-only residency plan from a compact motion definition."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def build_plan() -> dict[str, object]:
    duration = 150
    camera = {
        "type": "perspective",
        "position": [0, 0, -1000],
        "rotation_deg": [0, 0, 0],
        "fov_deg": 50,
        "near": 1,
        "far": 10000,
    }
    samples = []
    for frame in range(duration):
        sample_camera = dict(camera)
        sample_camera["position"] = [
            0,
            0,
            -1000.0 + 300.0 * frame / (duration - 1),
        ]
        samples.append({"frame": frame, "camera": sample_camera})

    return {
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 30,
            "fps_den": 1,
            "duration_frames": duration,
        },
        "job_id": "animated_image_residency_camera-only",
        "layers": [{
            "asset": "bright.png",
            "duration_frames": duration,
            "fit": "stretch",
            "id": "stage",
            "position": [0, 0],
            "size": [1920, 1080],
            "start_frame": 0,
            "type": "image",
            "enable_3d": True,
        }],
        "output": {"codec": "h264", "format": "mp4", "path": "camera-only.mp4"},
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "camera": camera,
        "camera_samples": samples,
    }


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} OUTPUT.plan.json", file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(build_plan(), indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
