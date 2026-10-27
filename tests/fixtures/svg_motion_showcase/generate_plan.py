#!/usr/bin/env python3
"""Expand the showcase camera motion into per-frame RenderPlan samples."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def build_plan(base_path: Path) -> dict[str, object]:
    plan = json.loads(base_path.read_text(encoding="utf-8"))
    duration = int(plan["canvas"]["duration_frames"])
    initial_camera = plan["camera"]
    samples = []
    for frame in range(duration):
        camera = json.loads(json.dumps(initial_camera))
        camera["position"][2] = -2600.0 + 220.0 * min(frame, 270) / 270.0
        samples.append({"frame": frame, "camera": camera})
    plan["camera_samples"] = samples
    return plan


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {Path(sys.argv[0]).name} BASE.plan.json OUTPUT.plan.json", file=sys.stderr)
        return 2
    base_path, output = map(Path, sys.argv[1:])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(build_plan(base_path), indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
