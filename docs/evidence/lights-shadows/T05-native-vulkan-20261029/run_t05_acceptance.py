#!/usr/bin/env python3
"""Pixel-level gate for the one-frame native Vulkan/NVENC T05 A/B shot."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


WIDTH = 1920
HEIGHT = 1080
CHANNELS = 4
PIXELS = WIDTH * HEIGHT
FRAME_BYTES = PIXELS * CHANNELS


def decode_rgba(ffmpeg: str, video: Path) -> list[bytes]:
    result = subprocess.run(
        [
            ffmpeg,
            "-v", "error",
            "-i", str(video),
            "-map", "0:v:0",
            "-f", "rawvideo",
            "-pix_fmt", "rgba",
            "-",
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        raise RuntimeError(
            f"ffmpeg decode failed for {video}: "
            f"{result.stderr.decode(errors='replace').strip()}"
        )
    if not result.stdout or len(result.stdout) % FRAME_BYTES:
        raise RuntimeError(
            f"{video}: decoded byte count {len(result.stdout)} is not a "
            f"non-zero multiple of {FRAME_BYTES} bytes/frame"
        )
    return [
        result.stdout[offset : offset + FRAME_BYTES]
        for offset in range(0, len(result.stdout), FRAME_BYTES)
    ]


def frame_metrics(frame: bytes) -> tuple[float, float, float]:
    mean_rgb = sum(sum(frame[channel::CHANNELS]) for channel in range(3)) / (
        3 * PIXELS
    )
    black_pixels = sum(
        1
        for offset in range(0, FRAME_BYTES, CHANNELS)
        if frame[offset] == frame[offset + 1] == frame[offset + 2] == 0
    )
    opaque_pixels = sum(
        1 for alpha in frame[3::CHANNELS] if alpha >= 250
    )
    return mean_rgb, black_pixels / PIXELS, opaque_pixels / PIXELS


def changed_ratio(first: bytes, second: bytes) -> float:
    changed = sum(
        1
        for offset in range(0, FRAME_BYTES, CHANNELS)
        if sum(
            abs(first[offset + channel] - second[offset + channel])
            for channel in range(3)
        ) > 8
    )
    return changed / PIXELS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--caster-on", action="append", type=Path, required=True)
    parser.add_argument("--caster-off", action="append", type=Path, required=True)
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--min-mean-rgb", type=float, default=20.0)
    parser.add_argument("--max-black-ratio", type=float, default=0.005)
    parser.add_argument("--min-opaque-ratio", type=float, default=0.99)
    parser.add_argument("--min-ab-changed-ratio", type=float, default=0.005)
    args = parser.parse_args()

    if not args.ffmpeg:
        parser.error("ffmpeg not found; pass --ffmpeg <path>")
    if len(args.caster_on) != len(args.caster_off):
        parser.error("provide the same number of caster-on and caster-off runs")

    try:
        passed = True
        for index, (on_path, off_path) in enumerate(
            zip(args.caster_on, args.caster_off), start=1
        ):
            on_frames = decode_rgba(args.ffmpeg, on_path)
            off_frames = decode_rgba(args.ffmpeg, off_path)
            if len(on_frames) != len(off_frames):
                raise RuntimeError(
                    f"run {index}: caster-on has {len(on_frames)} frames, "
                    f"caster-off has {len(off_frames)}"
                )

            for mode, frames in (("on", on_frames), ("off", off_frames)):
                for frame_index, frame in enumerate(frames):
                    mean_rgb, black_ratio, opaque_ratio = frame_metrics(frame)
                    ok = (
                        mean_rgb >= args.min_mean_rgb
                        and black_ratio <= args.max_black_ratio
                        and opaque_ratio >= args.min_opaque_ratio
                    )
                    passed &= ok
                    print(
                        f"run={index} mode={mode} frame={frame_index} "
                        f"mean_rgb={mean_rgb:.4f} "
                        f"black_ratio={black_ratio:.6f} "
                        f"opaque_ratio={opaque_ratio:.6f} "
                        f"{'PASS' if ok else 'FAIL'}"
                    )

            for frame_index, (on_frame, off_frame) in enumerate(
                zip(on_frames, off_frames)
            ):
                ratio = changed_ratio(on_frame, off_frame)
                ok = ratio >= args.min_ab_changed_ratio
                passed &= ok
                print(
                    f"run={index} frame={frame_index} "
                    f"ab_changed_ratio_gt8={ratio:.6f} "
                    f"{'PASS' if ok else 'FAIL'}"
                )
        return 0 if passed else 1
    except (OSError, RuntimeError) as error:
        print(f"T05 acceptance failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
