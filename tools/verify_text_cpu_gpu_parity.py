#!/usr/bin/env python3
"""CPU/GPU text pixel-truth gate (TICKET-TEXT-CLOSURE-20261109 T0-01).

Renders every manifest plan twice through the pre-encode raw sink:

    chronon3d_cli render --plan <case>.plan.json --assets-root <root> \
        --backend software --video-sink raw -o <out>/<case>/software.raw
    chronon3d_cli render --plan <case>.plan.json --assets-root <root> \
        --backend vulkan --gpu-hot-path-mode require_gpu_native \
        --video-sink raw -o <out>/<case>/vulkan.raw

The raw sink is RGBA8 premultiplied, frames concatenated with no container or
encoder.  The gate compares those bytes directly; the PNG diff is review-only.

Thresholds are the ticket's initial baseline.  A failing metric is a finding:
do not lower the threshold silently, either fix the renderer or document an
exception with an owner in the evidence REPORT.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

CHANNELS = 4
DEFAULT_THRESHOLDS = {
    "alpha_iou_min": 0.995,
    "coverage_ratio_min": 0.99,
    "coverage_ratio_max": 1.01,
    "rgb_mae_max": 2.0 / 255.0,
    "rgb_p99_max": 8.0 / 255.0,
    "ssim_luma_min": 0.98,
    "psnr_luma_min_db": 35.0,
    "flat_max": 2.0 / 255.0,
    "bbox_slack_px": 1,
    "edge_band_px": 2,
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(cmd: list[str], log_path: Path) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w") as log:
        log.write("$ " + " ".join(cmd) + "\n")
        log.flush()
        proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    return proc.returncode


def read_raw(path: Path, width: int, height: int, index: int) -> np.ndarray:
    per_frame = width * height * CHANNELS
    size = path.stat().st_size
    if size % per_frame != 0:
        raise ValueError(f"{path}: {size} bytes is not a whole number of {per_frame}-byte frames")
    count = size // per_frame
    if index >= count:
        raise ValueError(f"{path}: frame {index} requested, only {count} present")
    with path.open("rb") as handle:
        handle.seek(index * per_frame)
        data = handle.read(per_frame)
    return np.frombuffer(data, dtype=np.uint8).reshape(height, width, CHANNELS)


def bbox(mask: np.ndarray) -> tuple[int, int, int, int] | None:
    rows = np.flatnonzero(mask.any(axis=1))
    cols = np.flatnonzero(mask.any(axis=0))
    if rows.size == 0 or cols.size == 0:
        return None
    return int(cols[0]), int(rows[0]), int(cols[-1]), int(rows[-1])


def dilate(mask: np.ndarray, radius: int) -> np.ndarray:
    out = mask.copy()
    for _ in range(radius):
        grown = out.copy()
        grown[1:, :] |= out[:-1, :]
        grown[:-1, :] |= out[1:, :]
        grown[:, 1:] |= out[:, :-1]
        grown[:, :-1] |= out[:, 1:]
        out = grown
    return out


def metrics_for_frame(sw: np.ndarray, vk: np.ndarray, thresholds: dict,
                      roi_pad: int) -> dict:
    if sw.shape != vk.shape:
        raise ValueError(f"frame shape mismatch: {sw.shape} vs {vk.shape}")
    a_sw = sw[..., 3]
    a_vk = vk[..., 3]
    mask_sw = a_sw > 0
    mask_vk = a_vk > 0
    box_sw = bbox(mask_sw)
    box_vk = bbox(mask_vk)
    result: dict = {
        "bbox_software": box_sw,
        "bbox_vulkan": box_vk,
        "mask_pixels_software": int(mask_sw.sum()),
        "mask_pixels_vulkan": int(mask_vk.sum()),
        "pass": True,
        "failures": [],
    }

    def fail(message: str) -> None:
        result["pass"] = False
        result["failures"].append(message)

    if box_sw is None or box_vk is None:
        fail("empty ink mask")
        return result

    union_sw = mask_sw.sum()
    union_vk = mask_vk.sum()
    inter = np.logical_and(mask_sw, mask_vk).sum()
    union = np.logical_or(mask_sw, mask_vk).sum()
    alpha_iou = float(inter / union) if union else 1.0
    coverage = float(union_vk / union_sw) if union_sw else 1.0
    result["alpha_iou"] = alpha_iou
    result["coverage_ratio"] = coverage
    if alpha_iou < thresholds["alpha_iou_min"]:
        fail(f"alpha_iou {alpha_iou:.6f} < {thresholds['alpha_iou_min']}")
    if not (thresholds["coverage_ratio_min"] <= coverage <= thresholds["coverage_ratio_max"]):
        fail(f"coverage_ratio {coverage:.6f} outside threshold")

    for side, (a, b) in enumerate(zip(box_sw, box_vk)):
        if abs(a - b) > thresholds["bbox_slack_px"]:
            fail(f"bbox side {side} differs by {abs(a - b)}px")
            break

    h, w = mask_sw.shape
    x0 = max(0, min(box_sw[0], box_vk[0]) - roi_pad)
    y0 = max(0, min(box_sw[1], box_vk[1]) - roi_pad)
    x1 = min(w, max(box_sw[2], box_vk[2]) + roi_pad + 1)
    y1 = min(h, max(box_sw[3], box_vk[3]) + roi_pad + 1)
    roi_sw = sw[y0:y1, x0:x1]
    roi_vk = vk[y0:y1, x0:x1]
    rgb_sw = roi_sw[..., :3].astype(np.float32) / 255.0
    rgb_vk = roi_vk[..., :3].astype(np.float32) / 255.0
    diff = np.abs(rgb_sw - rgb_vk)
    mae = float(diff.mean())
    p95 = float(np.percentile(diff, 95))
    p99 = float(np.percentile(diff, 99))
    rgb_max = float(diff.max())
    result["roi"] = [x0, y0, x1, y1]
    result["rgb_mae"] = mae
    result["rgb_p95"] = p95
    result["rgb_p99"] = p99
    result["rgb_max"] = rgb_max
    if mae > thresholds["rgb_mae_max"]:
        fail(f"rgb_mae {mae:.6f} > {thresholds['rgb_mae_max']}")
    if p99 > thresholds["rgb_p99_max"]:
        fail(f"rgb_p99 {p99:.6f} > {thresholds['rgb_p99_max']}")

    luma_sw = (0.2126 * rgb_sw[..., 0] + 0.7152 * rgb_sw[..., 1] +
               0.0722 * rgb_sw[..., 2])
    luma_vk = (0.2126 * rgb_vk[..., 0] + 0.7152 * rgb_vk[..., 1] +
               0.0722 * rgb_vk[..., 2])
    mse = float(np.mean((luma_sw - luma_vk) ** 2))
    psnr = 99.0 if mse <= 1e-12 else float(10.0 * math.log10(1.0 / mse))
    mu_a = float(luma_sw.mean())
    mu_b = float(luma_vk.mean())
    var_a = float(luma_sw.var())
    var_b = float(luma_vk.var())
    cov = float(((luma_sw - mu_a) * (luma_vk - mu_b)).mean())
    c1 = 0.01 ** 2
    c2 = 0.03 ** 2
    ssim = float(((2 * mu_a * mu_b + c1) * (2 * cov + c2)) /
                 ((mu_a * mu_a + mu_b * mu_b + c1) * (var_a + var_b + c2)))
    result["psnr_luma_db"] = psnr
    result["ssim_luma"] = ssim
    if psnr < thresholds["psnr_luma_min_db"]:
        fail(f"psnr_luma {psnr:.3f} dB < {thresholds['psnr_luma_min_db']}")
    if ssim < thresholds["ssim_luma_min"]:
        fail(f"ssim_luma {ssim:.6f} < {thresholds['ssim_luma_min']}")

    # Outside the 2 px edge band both images must stay flat: no stray glyph
    # plane, shadow rectangle or atlas page may leak into the background.
    edge = dilate(np.logical_or(mask_sw, mask_vk), thresholds["edge_band_px"])
    flat = ~edge
    if flat.any():
        flat_rgb = np.abs(sw[..., :3].astype(np.float32) / 255.0 -
                          vk[..., :3].astype(np.float32) / 255.0)[flat]
        flat_alpha = np.abs(sw[..., 3].astype(np.float32) -
                            vk[..., 3].astype(np.float32)) / 255.0
        flat_max = float(max(flat_rgb.max() if flat_rgb.size else 0.0,
                             flat_alpha[flat].max() if flat_alpha[flat].size else 0.0))
        result["flat_max_outside_edge"] = flat_max
        if flat_max > thresholds["flat_max"]:
            fail(f"flat background differs by {flat_max:.6f}")
    return result


def write_diff_png(sw: np.ndarray, vk: np.ndarray, path: Path) -> None:
    rgb_sw = sw[..., :3].astype(np.int16)
    rgb_vk = vk[..., :3].astype(np.int16)
    diff = np.abs(rgb_sw - rgb_vk)
    amplified = np.clip(diff * 8, 0, 255).astype(np.uint8)
    rgba = np.dstack([amplified, np.full(diff.shape[:2], 255, np.uint8)])
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba, mode="RGBA").save(path)


def vulkan_identity() -> str:
    exe = shutil.which("vulkaninfo")
    if not exe:
        return "vulkaninfo not available"
    try:
        out = subprocess.run([exe, "--summary"], capture_output=True, text=True,
                             timeout=20).stdout
    except Exception as exc:  # noqa: BLE001 - report, never hide
        return f"vulkaninfo failed: {exc}"
    lines = [line.strip() for line in out.splitlines() if line.strip()]
    return "\n".join(lines[:24])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--assets-root", type=Path, default=None)
    parser.add_argument("--cli", type=Path, default=None)
    parser.add_argument("--only", default=None)
    parser.add_argument("--skip-gpu", action="store_true")
    parser.add_argument("--repeat", action="store_true",
                        help="Render software twice and require identical bytes")
    parser.add_argument("--require-same-sha", action="store_true")
    args = parser.parse_args()

    root = args.corpus.resolve().parent
    assets_root = (args.assets_root or Path.cwd()).resolve()
    cli = (args.cli or (Path.cwd() / "build/chronon/linux-video-release/apps/"
                        "chronon3d_cli/chronon3d_cli")).resolve()
    if not cli.exists():
        print(f"missing CLI: {cli}", file=sys.stderr)
        return 2
    manifest = json.loads(args.corpus.read_text())
    cases = manifest["cases"]
    if args.only:
        cases = [case for case in cases if case["id"] == args.only]
        if not cases:
            print(f"no case named {args.only}", file=sys.stderr)
            return 2

    args.out.mkdir(parents=True, exist_ok=True)
    thresholds = dict(DEFAULT_THRESHOLDS)
    thresholds.update(manifest.get("thresholds", {}))
    report = {
        "corpus": str(args.corpus.resolve()),
        "cli": str(cli),
        "cli_sha256": sha256_file(cli),
        "vulkan_identity": vulkan_identity(),
        "thresholds": thresholds,
        "cases": {},
        "pass": True,
    }

    for case in cases:
        case_id = case["id"]
        plan = root / case["plan"]
        if not plan.exists():
            report["cases"][case_id] = {"pass": False, "failures": [f"missing plan {plan}"]}
            report["pass"] = False
            continue
        plan_sha = sha256_file(plan)
        case_report: dict = {"plan": str(plan), "plan_sha256": plan_sha,
                             "frames": {}, "pass": True, "failures": []}
        expected_sha = case.get("plan_sha256")
        if args.require_same_sha and expected_sha and expected_sha != plan_sha:
            case_report["pass"] = False
            case_report["failures"].append("plan SHA differs from the frozen manifest")
        if args.require_same_sha and not expected_sha:
            case_report["pass"] = False
            case_report["failures"].append("manifest has no frozen plan_sha256")

        plan_doc = json.loads(plan.read_text())
        width = int(plan_doc["canvas"]["width"])
        height = int(plan_doc["canvas"]["height"])
        duration = int(plan_doc["canvas"]["duration_frames"])
        case_dir = args.out / case_id
        case_dir.mkdir(parents=True, exist_ok=True)
        backends = {"software": case_dir / "software.raw"}
        if not args.skip_gpu:
            backends["vulkan"] = case_dir / "vulkan.raw"

        for backend, raw_path in backends.items():
            cmd = [str(cli), "render", "--plan", str(plan),
                   "--assets-root", str(assets_root), "--backend", backend,
                   "--video-sink", "raw", "--profile", "production",
                   "-o", str(raw_path), "-v", "warn"]
            if backend == "vulkan":
                cmd[cmd.index("--backend") + 1] = "vulkan"
                cmd.extend(["--gpu-hot-path-mode", "require_gpu_native"])
                # The route evidence is an info line; warnings alone are not
                # enough to prove the native MTSDF path was taken.
                cmd[cmd.index("-v") + 1] = "info"
            code = run(cmd, case_dir / f"{backend}.log")
            if code != 0:
                case_report["pass"] = False
                case_report["failures"].append(f"{backend} render exited {code}")
                continue
            per_frame = width * height * CHANNELS
            if raw_path.stat().st_size % per_frame != 0:
                case_report["pass"] = False
                case_report["failures"].append(f"{backend} raw size is not frame-aligned")
            if backend == "vulkan":
                log_text = (case_dir / f"{backend}.log").read_text(errors="replace")
                lowered = log_text.lower()
                if "route=mtsdf" not in log_text:
                    case_report["pass"] = False
                    case_report["failures"].append("vulkan log missing route=mtsdf")
                # `font-fallback` is a benign asset warning, not a render
                # route fallback. Only the native-surface error is a gate hit.
                if ("native surface path fell back" in lowered or
                        "unsupportedcapability" in lowered):
                    case_report["pass"] = False
                    case_report["failures"].append(
                        "vulkan log reports fallback/UnsupportedCapability")
        if not case_report["pass"]:
            report["cases"][case_id] = case_report
            report["pass"] = False
            continue

        if args.repeat:
            repeat_path = case_dir / "software.repeat.raw"
            cmd = [str(cli), "render", "--plan", str(plan),
                   "--assets-root", str(assets_root), "--backend", "software",
                   "--video-sink", "raw", "--profile", "production",
                   "-o", str(repeat_path), "-v", "warn"]
            code = run(cmd, case_dir / "software.repeat.log")
            if code != 0:
                case_report["pass"] = False
                case_report["failures"].append(f"software repeat exited {code}")
            else:
                same = sha256_file(repeat_path) == sha256_file(case_dir / "software.raw")
                case_report["repeat_software_identical"] = same
                if not same:
                    case_report["pass"] = False
                    case_report["failures"].append("software repeat bytes differ")

        if args.skip_gpu:
            report["cases"][case_id] = case_report
            continue

        frames = case.get("frames") or list(range(min(duration, 120)))
        worst = {}
        for frame_index in frames:
            if frame_index >= duration:
                case_report["pass"] = False
                case_report["failures"].append(f"frame {frame_index} outside plan duration")
                break
            sw = read_raw(case_dir / "software.raw", width, height, frame_index)
            vk = read_raw(case_dir / "vulkan.raw", width, height, frame_index)
            frame_metrics = metrics_for_frame(sw, vk, thresholds,
                                              int(case.get("roi_pad", 24)))
            case_report["frames"][str(frame_index)] = frame_metrics
            if not frame_metrics["pass"]:
                case_report["pass"] = False
                write_diff_png(sw, vk, case_dir / f"diff_{frame_index:04d}.png")
            for key in ("alpha_iou", "coverage_ratio", "rgb_mae", "rgb_p95",
                        "rgb_p99", "rgb_max", "ssim_luma", "psnr_luma_db"):
                if key not in frame_metrics:
                    continue
                if key not in worst:
                    worst[key] = frame_metrics[key]
                elif key in ("alpha_iou", "coverage_ratio", "ssim_luma"):
                    worst[key] = min(worst[key], frame_metrics[key])
                else:
                    worst[key] = max(worst[key], frame_metrics[key])
        case_report["worst"] = worst
        report["cases"][case_id] = case_report
        if not case_report["pass"]:
            report["pass"] = False

    report_path = args.out / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"pass": report["pass"],
                      "report": str(report_path),
                      "cases": {k: v["pass"] for k, v in report["cases"].items()}},
                     indent=2))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
