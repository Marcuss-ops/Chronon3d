#!/usr/bin/env python3
"""Verify frame count, black frames and Vulkan validation for raw RGBA output."""

import argparse
import hashlib
import json
import re
from pathlib import Path

import numpy as np


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("validation_log", type=Path)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--frames", type=int, default=150)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    frame_bytes = args.width * args.height * 4
    expected_bytes = frame_bytes * args.frames
    actual_bytes = args.raw.stat().st_size
    raw_hasher = hashlib.sha256()
    with args.raw.open("rb") as raw_stream:
        while chunk := raw_stream.read(8 * 1024 * 1024):
            raw_hasher.update(chunk)
    result = {
        "raw_path": str(args.raw),
        "width": args.width,
        "height": args.height,
        "format": "RGBA8",
        "expected_frames": args.frames,
        "expected_bytes": expected_bytes,
        "actual_bytes": actual_bytes,
        "raw_sha256": raw_hasher.hexdigest(),
        "frame_hashes": [],
        "black_frames": [],
    }
    if actual_bytes == expected_bytes:
        frames = np.memmap(
            args.raw, dtype=np.uint8, mode="r",
            shape=(args.frames, args.height, args.width, 4))
        for index in range(args.frames):
            rgba = frames[index]
            rgb = rgba[:, :, :3]
            result["frame_hashes"].append(
                hashlib.sha256(memoryview(rgba)).hexdigest())
            nonzero = int(np.count_nonzero(np.any(rgb != 0, axis=2)))
            result.setdefault("frames", []).append({
                "index": index,
                "rgb_min": int(rgb.min()),
                "rgb_max": int(rgb.max()),
                "rgb_mean": float(rgb.mean()),
                "nonblack_pixels": nonzero,
                "alpha_min": int(rgba[:, :, 3].min()),
                "alpha_max": int(rgba[:, :, 3].max()),
            })
            if nonzero == 0:
                result["black_frames"].append(index)
        del frames

    log = args.validation_log.read_text(errors="replace")
    validation_matches = re.findall(
        r"Vulkan Validation ERROR|Validation Error|VUID-", log)
    result["validation_error_matches"] = len(validation_matches)
    result["unique_frame_hashes"] = len(set(result["frame_hashes"]))
    result["frame_count_gate"] = (
        "PASS" if actual_bytes == expected_bytes else "FAIL")
    result["black_frame_gate"] = (
        "PASS" if actual_bytes == expected_bytes and not result["black_frames"]
        else "FAIL")
    result["validation_gate"] = (
        "PASS" if not validation_matches else "FAIL")
    result["aggregate_gate"] = (
        "PASS" if all(result[key] == "PASS" for key in (
            "frame_count_gate", "black_frame_gate", "validation_gate"))
        else "FAIL")

    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items()
                      if key != "frames" and key != "frame_hashes"}, indent=2))
    return 0 if result["aggregate_gate"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
