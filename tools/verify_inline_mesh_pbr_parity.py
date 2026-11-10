#!/usr/bin/env python3
"""E1 gate: Vulkan inline-mesh metallic-roughness material parity.

Renders one V3 plan that contains four inline meshes with the same base color
and different metallic-roughness maps (rough dielectric, smooth dielectric,
metal at roughness 0.5, dielectric at roughness 0.5) through the pre-encode
raw sink on both backends, then checks:

  * the G channel changes the pixels (rough vs smooth),
  * the B channel changes the pixels (metal vs dielectric),
  * the software and Vulkan lanes agree over the mesh interiors,
  * the native Vulkan route does not fall back.

Thresholds are fixed here, before the first run; a failure is a finding, not a
reason to loosen the gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

WIDTH = 640
HEIGHT = 360
CHANNELS = 4
BANDS = {
    "rough": (100, 180),
    "smooth": (220, 300),
    "metal": (340, 420),
    "dielectric": (460, 540),
}
THRESHOLDS = {
    "alpha_iou_min": 0.99,
    "rgb_mae_max": 2.0 / 255.0,
    "rgb_p99_max": 8.0 / 255.0,
    "material_luma_delta_min": 1.0 / 255.0,
    "min_band_pixels": 200,
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_raw(path: Path) -> np.ndarray:
    per_frame = WIDTH * HEIGHT * CHANNELS
    size = path.stat().st_size
    if size < per_frame:
        raise ValueError(f"{path}: {size} bytes, expected at least {per_frame}")
    with path.open("rb") as handle:
        data = handle.read(per_frame)
    return np.frombuffer(data, dtype=np.uint8).reshape(HEIGHT, WIDTH, CHANNELS)


def render(cli: Path, plan: Path, assets_root: Path, backend: str, out: Path,
           log: Path) -> None:
    cmd = [str(cli), "render", "--plan", str(plan),
           "--assets-root", str(assets_root), "--backend", backend,
           "--video-sink", "raw", "--profile", "production",
           "-o", str(out), "-v", "info" if backend == "vulkan" else "warn"]
    if backend == "vulkan":
        cmd.extend(["--gpu-hot-path-mode", "require_gpu_native"])
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as handle:
        handle.write("$ " + " ".join(cmd) + "\n")
        handle.flush()
        result = subprocess.run(cmd, stdout=handle, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise RuntimeError(f"{backend} render exited {result.returncode}; see {log}")


def erode(mask: np.ndarray, radius: int) -> np.ndarray:
    out = mask.copy()
    for _ in range(radius):
        grown = out.copy()
        grown[1:, :] &= out[:-1, :]
        grown[:-1, :] &= out[1:, :]
        grown[:, 1:] &= out[:, :-1]
        grown[:, :-1] &= out[:, 1:]
        out = grown
    return out


def luma(rgb: np.ndarray) -> np.ndarray:
    values = rgb.astype(np.float64) / 255.0
    return 0.2126 * values[..., 0] + 0.7152 * values[..., 1] + 0.0722 * values[..., 2]


def band_stats(frame: np.ndarray, x0: int, x1: int) -> dict:
    band = frame[:, x0:x1]
    mask = band[..., 3] > 127
    count = int(mask.sum())
    if count == 0:
        return {"pixels": 0, "mean_luma": None}
    return {"pixels": count, "mean_luma": float(luma(band[..., :3])[mask].mean())}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--assets-root", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--cli", type=Path, default=None)
    args = parser.parse_args()

    cli = (args.cli or (Path.cwd() / "build/chronon/linux-video-release/apps/"
                        "chronon3d_cli/chronon3d_cli")).resolve()
    if not cli.exists():
        print(f"missing CLI: {cli}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    raws: dict[str, Path] = {}
    for backend in ("software", "vulkan"):
        raw = args.out / f"{backend}.raw"
        log = args.out / f"{backend}.log"
        render(cli, args.plan.resolve(), args.assets_root.resolve(), backend, raw, log)
        raws[backend] = raw

    report: dict = {
        "plan": str(args.plan.resolve()),
        "plan_sha256": sha256_file(args.plan),
        "cli": str(cli),
        "cli_sha256": sha256_file(cli),
        "thresholds": THRESHOLDS,
        "bands": BANDS,
        "pass": True,
        "failures": [],
    }

    def fail(message: str) -> None:
        report["pass"] = False
        report["failures"].append(message)

    frames = {name: read_raw(path) for name, path in raws.items()}
    report["raw_sha256"] = {name: sha256_file(path) for name, path in raws.items()}

    vulkan_log = (args.out / "vulkan.log").read_text(errors="replace")
    if "route=mtsdf" in vulkan_log:
        pass
    if "native surface path fell back" in vulkan_log.lower():
        fail("vulkan log reports a native-surface fallback")
    if "unsupportedcapability" in vulkan_log.lower():
        fail("vulkan log reports UnsupportedCapability")

    stats: dict = {}
    for backend, frame in frames.items():
        stats[backend] = {name: band_stats(frame, *band)
                          for name, band in BANDS.items()}
        for name, values in stats[backend].items():
            if values["pixels"] < THRESHOLDS["min_band_pixels"]:
                fail(f"{backend}:{name} has only {values['pixels']} covered pixels")
    report["band_stats"] = stats

    if report["pass"]:
        for backend in ("software", "vulkan"):
            rough = stats[backend]["rough"]["mean_luma"]
            smooth = stats[backend]["smooth"]["mean_luma"]
            metal = stats[backend]["metal"]["mean_luma"]
            dielectric = stats[backend]["dielectric"]["mean_luma"]
            delta_roughness = abs(rough - smooth)
            delta_metallic = abs(metal - dielectric)
            report[f"{backend}_roughness_luma_delta"] = delta_roughness
            report[f"{backend}_metallic_luma_delta"] = delta_metallic
            if delta_roughness < THRESHOLDS["material_luma_delta_min"]:
                fail(f"{backend}: G channel (roughness) does not change the pixels")
            if delta_metallic < THRESHOLDS["material_luma_delta_min"]:
                fail(f"{backend}: B channel (metallic) does not change the pixels")

    sw = frames["software"]
    vk = frames["vulkan"]
    mask_sw = sw[..., 3] > 0
    mask_vk = vk[..., 3] > 0
    union = mask_sw | mask_vk
    intersection = int((mask_sw & mask_vk).sum())
    union_count = int(union.sum())
    report["alpha_iou"] = intersection / union_count if union_count else 1.0
    if report["alpha_iou"] < THRESHOLDS["alpha_iou_min"]:
        fail(f"alpha IoU {report['alpha_iou']:.6f} below {THRESHOLDS['alpha_iou_min']}")

    interior = erode(union, 3)
    if not interior.any():
        fail("no interior pixels to compare")
    else:
        diff = np.abs(sw[..., :3].astype(np.int16) - vk[..., :3].astype(np.int16))
        values = diff[interior]
        mae = float(values.mean()) / 255.0
        p99 = float(np.percentile(values, 99)) / 255.0
        report["rgb_interior_mae"] = mae
        report["rgb_interior_p99"] = p99
        if mae > THRESHOLDS["rgb_mae_max"]:
            fail(f"interior RGB MAE {mae:.6f} above {THRESHOLDS['rgb_mae_max']}")
        if p99 > THRESHOLDS["rgb_p99_max"]:
            fail(f"interior RGB p99 {p99:.6f} above {THRESHOLDS['rgb_p99_max']}")

    report_path = args.out / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"pass": report["pass"], "report": str(report_path),
                      "failures": report["failures"]}, indent=2))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
