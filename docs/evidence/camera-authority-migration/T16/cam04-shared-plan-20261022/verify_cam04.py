#!/usr/bin/env python3
"""Recheck stored C3D/threepp RGB samples for the CAM-04 shared-plan gate."""
import hashlib
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 1920, 1080
FRAMES = (0, 30, 75, 120, 149)
MAX_MAE = 1.70
MAX_P99 = 22.0


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    results = []
    for frame in FRAMES:
        c3d_path = ROOT / f"c3d_frame_{frame}.rgba"
        threepp_path = ROOT / f"frame_{frame}.rgb"
        c3d_bytes = np.fromfile(c3d_path, dtype=np.uint8)
        threepp_bytes = np.fromfile(threepp_path, dtype=np.uint8)
        expected_rgba = WIDTH * HEIGHT * 4
        expected_rgb = WIDTH * HEIGHT * 3
        if c3d_bytes.size != expected_rgba or threepp_bytes.size != expected_rgb:
            raise SystemExit(f"CAM-04 FAIL frame {frame}: invalid raw byte count")
        c3d = c3d_bytes.reshape(HEIGHT, WIDTH, 4)[:, :, :3].astype(np.int16)
        # OpenGL readRGBPixels is bottom-up; C3D raw rows are top-down.
        threepp = threepp_bytes.reshape(HEIGHT, WIDTH, 3)[::-1].astype(np.int16)
        delta = np.abs(c3d - threepp)
        mae = float(delta.mean())
        p99 = float(np.percentile(delta, 99))
        maximum = int(delta.max())
        results.append({
            "frame": frame,
            "rgb_mae": mae,
            "rgb_p99": p99,
            "rgb_max": maximum,
            "c3d_rgba_sha256": sha256(c3d_path),
            "threepp_rgb_sha256": sha256(threepp_path),
            "pass": mae <= MAX_MAE and p99 <= MAX_P99,
        })
        print(f"frame={frame} MAE={mae:.4f} p99={p99:.0f} max={maximum} "
              f"{'PASS' if results[-1]['pass'] else 'FAIL'}")
    report = {
        "gate": "CAM-04",
        "plan_sha256": sha256(ROOT.parent / "scene.camera.three_depth.plan.json"),
        "resolution": [WIDTH, HEIGHT],
        "frames": list(FRAMES),
        "limits": {"rgb_mae_max": MAX_MAE, "rgb_p99_max": MAX_P99},
        "results": results,
        "status": "PASS" if all(row["pass"] for row in results) else "FAIL",
    }
    (ROOT / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    if report["status"] != "PASS":
        raise SystemExit("CAM-04 FAIL")
    print("CAM-04 PASS: the five selected frames satisfy both declared limits")


if __name__ == "__main__":
    main()
