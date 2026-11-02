#!/usr/bin/env python3
"""Verify the current strict-native Optical Studio render without loosening gates."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / "measurement-contract.json").read_text())
ROIS = CONTRACT["roi_xyxy"]


def run(args):
    return subprocess.run(args, check=True, capture_output=True)


def energy(rgb, roi):
    x0, y0, x1, y1 = roi
    crop = rgb[y0:y1, x0:x1].astype(np.float64)
    gray = crop @ np.array([0.2126, 0.7152, 0.0722])
    return float(np.mean(np.diff(gray, axis=0) ** 2) +
                 np.mean(np.diff(gray, axis=1) ** 2))


def read_frame(path, frame_index):
    selected = run([
        "ffmpeg", "-v", "error", "-i", str(path), "-vf",
        f"select='eq(n,{frame_index})'", "-frames:v", "1", "-f", "rawvideo",
        "-pix_fmt", "rgb24", "-",
    ]).stdout
    rgb = np.frombuffer(selected, dtype=np.uint8)
    return rgb.reshape(1080, 1920, 3)


def read_raw_rgba(path):
    data = np.fromfile(path, dtype=np.uint8)
    expected = 1920 * 1080 * 4
    if data.size != expected:
        raise ValueError(f"{path}: expected {expected} RGBA bytes, got {data.size}")
    return data.reshape(1080, 1920, 4)[:, :, :3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dof-video", type=Path, required=True)
    parser.add_argument("--sharp-frame-template", required=True,
                        help="Full DoF-off video path, or a path pattern containing {frame}")
    parser.add_argument("--validation-log", type=Path, required=True)
    parser.add_argument("--raw-focus-dir", type=Path, required=True,
                        help="Contains dof-{frame}.rgba and sharp-{frame}.rgba raw Vulkan readbacks")
    parser.add_argument("--plan-sha256", required=True)
    parser.add_argument("--report", type=Path, default=ROOT / "strict-native-current-pair-report.json")
    args = parser.parse_args()

    probe = json.loads(run([
        "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height,r_frame_rate,nb_read_frames",
        "-of", "json", str(args.dof_video),
    ]).stdout)
    stream = probe["streams"][0]
    decode = run(["ffmpeg", "-v", "error", "-i", str(args.dof_video), "-f", "null", "-"])
    black = run([
        "ffmpeg", "-hide_banner", "-i", str(args.dof_video), "-vf",
        "blackdetect=d=0.033:pix_th=0.10:pic_th=0.98", "-an", "-f", "null", "-",
    ])
    black_lines = [line for line in black.stderr.decode().splitlines() if "black_start:" in line]
    validation_errors = args.validation_log.read_text(errors="replace").count("Vulkan Validation ERROR")
    output_pass = (stream["codec_name"] == "h264" and stream["width"] == 1920 and
                   stream["height"] == 1080 and stream["r_frame_rate"] == "30/1" and
                   int(stream["nb_read_frames"]) >= 150 and not decode.stderr and not black_lines)

    focus_video_diagnostic = {}
    focus_raw = {}
    raw_sha256 = {}
    for frame_index, expected_focus in ((30, "near"), (75, "middle"), (149, "far")):
        on = read_frame(args.dof_video, frame_index)
        sharp_is_sentinel = "{frame}" in args.sharp_frame_template
        sharp_path = Path(args.sharp_frame_template.format(frame=frame_index))
        off = read_frame(sharp_path, 0 if sharp_is_sentinel else frame_index)
        Image.fromarray(on).save(ROOT / f"current-pair-dof-{frame_index:03}.png")
        Image.fromarray(off).save(ROOT / f"current-pair-sharp-{frame_index:03}.png")
        on_raw = read_raw_rgba(args.raw_focus_dir / f"dof-{frame_index}.rgba")
        off_raw = read_raw_rgba(args.raw_focus_dir / f"sharp-{frame_index}.rgba")
        raw_sha256[str(frame_index)] = {
            "dof": hashlib.sha256((args.raw_focus_dir / f"dof-{frame_index}.rgba").read_bytes()).hexdigest(),
            "sharp": hashlib.sha256((args.raw_focus_dir / f"sharp-{frame_index}.rgba").read_bytes()).hexdigest(),
        }
        Image.fromarray(on_raw).save(ROOT / f"current-pair-raw-dof-{frame_index:03}.png")
        Image.fromarray(off_raw).save(ROOT / f"current-pair-raw-sharp-{frame_index:03}.png")
        video_results, raw_results = {}, {}
        for name, roi in ROIS.items():
            on_energy, off_energy = energy(on, roi), energy(off, roi)
            ratio = on_energy / off_energy if off_energy > 0.0 else None
            passed = (ratio >= 0.70 if name == expected_focus else ratio <= 0.90) if ratio is not None else False
            video_results[name] = {
                "dof_edge_energy": on_energy,
                "sharp_edge_energy": off_energy,
                "on_off_ratio": ratio,
                "expected_focused": name == expected_focus,
                "gate": "PASS" if passed else "FAIL",
            }
            on_energy, off_energy = energy(on_raw, roi), energy(off_raw, roi)
            ratio = on_energy / off_energy if off_energy > 0.0 else None
            passed = (ratio >= 0.70 if name == expected_focus else ratio <= 0.90) if ratio is not None else False
            raw_results[name] = {
                "dof_edge_energy": on_energy,
                "sharp_edge_energy": off_energy,
                "on_off_ratio": ratio,
                "expected_focused": name == expected_focus,
                "gate": "PASS" if passed else "FAIL",
            }
        focus_video_diagnostic[str(frame_index)] = video_results
        focus_raw[str(frame_index)] = raw_results

    focus_pass = all(item["gate"] == "PASS" for frame in focus_raw.values() for item in frame.values())
    report = {
        "plan_sha256": args.plan_sha256,
        "video_sha256": hashlib.sha256(args.dof_video.read_bytes()).hexdigest(),
        "video_bytes": args.dof_video.stat().st_size,
        "stream": stream,
        "decode_errors": decode.stderr.decode(),
        "black_intervals": black_lines,
        "vulkan_validation_errors": validation_errors,
        "single_flight": True,
        "output_gate": "PASS" if output_pass and validation_errors == 0 else "FAIL",
        "focus_gate": "PASS" if focus_pass else "FAIL",
        "focus_metrics_raw": focus_raw,
        "focus_metrics_video_lossy_diagnostic": focus_video_diagnostic,
        "raw_rgba_sha256": raw_sha256,
        "note": "Single-flight strict-native evidence does not certify asynchronous Vulkan surface lifetime or human review.",
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["output_gate"] == "PASS" and report["focus_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
