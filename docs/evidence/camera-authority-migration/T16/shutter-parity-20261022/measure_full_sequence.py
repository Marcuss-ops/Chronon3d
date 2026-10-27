#!/usr/bin/env python3
"""Compare every decoded GPU shutter frame with its software RGBA reference."""

import argparse
import json
import subprocess
from pathlib import Path

import numpy as np


def probe_video(video: Path) -> int:
    raw = subprocess.check_output(
        [
            "ffprobe", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=width,height,nb_frames",
            "-of", "json", str(video),
        ],
        text=True,
    )
    stream = json.loads(raw)["streams"][0]
    if (int(stream["width"]), int(stream["height"])) != (1920, 1080):
        raise SystemExit(f"unexpected GPU video dimensions: {stream}")
    return int(stream["nb_frames"])


def metrics(a: np.ndarray, b: np.ndarray) -> dict[str, float | int]:
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return {
        "mae": float(diff.mean()),
        "p99_absolute_error": float(np.percentile(diff, 99)),
        "max_absolute_error": int(diff.max()),
        "changed_channel_percent": float(np.mean(diff != 0) * 100.0),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--software", type=Path, required=True,
                        help="150-frame raw RGBA8 software output")
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--gpu-video", type=Path,
                        help="Vulkan/NVENC MP4; decoded before comparison")
    output.add_argument("--gpu-raw", type=Path,
                        help="Vulkan raw RGBA8 output; compared before encode")
    parser.add_argument("--metrics", type=Path, required=True)
    args = parser.parse_args()

    width, height = 1920, 1080
    pixels = width * height
    frames = probe_video(args.gpu_video) if args.gpu_video else 150
    expected_bytes = frames * pixels * 4
    if args.software.stat().st_size != expected_bytes:
        raise SystemExit(
            f"software RGBA size {args.software.stat().st_size} != {expected_bytes}"
        )
    reference = np.memmap(
        args.software, dtype=np.uint8, mode="r", shape=(frames, height, width, 4)
    )

    gpu_raw = None
    decoder = None
    if args.gpu_raw:
        if args.gpu_raw.stat().st_size != expected_bytes:
            raise SystemExit(
                f"GPU RGBA size {args.gpu_raw.stat().st_size} != {expected_bytes}"
            )
        gpu_raw = np.memmap(
            args.gpu_raw, dtype=np.uint8, mode="r",
            shape=(frames, height, width, 4),
        )
    else:
        decoder = subprocess.Popen(
            [
                "ffmpeg", "-v", "error", "-i", str(args.gpu_video),
                "-map", "0:v:0", "-vsync", "0", "-f", "rawvideo",
                "-pix_fmt", "rgb24", "pipe:1",
            ],
            stdout=subprocess.PIPE,
        )
        assert decoder.stdout is not None
    frame_bytes = pixels * (3 if decoder else 4)
    per_frame = []
    for index in range(frames):
        if gpu_raw is not None:
            gpu = gpu_raw[index, :, :, :3]
        else:
            assert decoder is not None and decoder.stdout is not None
            data = decoder.stdout.read(frame_bytes)
            if len(data) != frame_bytes:
                decoder.kill()
                raise SystemExit(f"decoded only {index} complete RGB frames")
            gpu = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 3)
        per_frame.append(metrics(reference[index, :, :, :3], gpu))
    if decoder is not None:
        assert decoder.stdout is not None
        decoder.stdout.close()
        if decoder.wait() != 0:
            raise SystemExit("ffmpeg failed while decoding the GPU video")

    mean_mae = float(np.mean([row["mae"] for row in per_frame]))
    max_mae = float(max(row["mae"] for row in per_frame))
    max_p99 = float(max(row["p99_absolute_error"] for row in per_frame))
    result = {
        "scope": "all frames; T16 three-depth RenderPlan; 1920x1080",
        "frames": frames,
        "software_reference": str(args.software),
        "gpu_video": str(args.gpu_video) if args.gpu_video else None,
        "gpu_raw": str(args.gpu_raw) if args.gpu_raw else None,
        "gpu_path": (
            "Vulkan raw RGBA readback before encode"
            if args.gpu_raw
            else "Vulkan require_gpu_native_full_graph + H.264 NVENC"
        ),
        "shutter": {"angle_degrees": 90, "samples": 8},
        "pixel_gate": {"mae_max": 2.0, "p99_absolute_error_max": 5.0},
        "mean_per_frame_mae": mean_mae,
        "max_per_frame_mae": max_mae,
        "max_per_frame_p99_absolute_error": max_p99,
        "frames_passing_gate": sum(
            row["mae"] <= 2.0 and row["p99_absolute_error"] <= 5.0
            for row in per_frame
        ),
        "per_frame": per_frame,
    }
    result["pixel_gate"]["status"] = (
        "PASS" if result["frames_passing_gate"] == frames else "FAIL"
    )
    args.metrics.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items()
                      if key != "per_frame"}, indent=2))
    if result["pixel_gate"]["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
