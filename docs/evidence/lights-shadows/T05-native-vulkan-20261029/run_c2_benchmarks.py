#!/usr/bin/env python3
"""Run the three strict-Vulkan C2 one-frame workloads and save measurements."""

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = Path(__file__).resolve().parents[4]
PLANS = HERE
RESULT = HERE / "c2-results.json"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/chronon3d_cli", file=sys.stderr)
        return 2
    cli = Path(sys.argv[1]).resolve()
    if not cli.is_file():
        print(f"CLI not found: {cli}", file=sys.stderr)
        return 2

    rows = []
    for count in (1_000, 10_000, 100_000):
        plan = PLANS / f"c2-{count}.plan.json"
        output = HERE / f"c2-{count}.mp4"
        log = HERE / f"c2-{count}.log"
        command = [
            str(cli), "render", "--plan", str(plan),
            "--start-frame", "0", "--end-frame", "0",
            "--output", str(output), "--assets-root", str(ROOT),
            "--backend", "vulkan", "--gpu-hot-path-mode", "require_gpu_native",
            "--hardware", "nvenc", "--encoder-backend", "native",
            "--log-level", "info",
        ]
        environment = os.environ.copy()
        started = time.perf_counter()
        with log.open("wb") as stream:
            process = subprocess.run(command, cwd=ROOT, env=environment,
                                     stdout=stream, stderr=subprocess.STDOUT,
                                     check=False)
        elapsed = time.perf_counter() - started
        decoded = subprocess.run(
            ["ffmpeg", "-y", "-v", "error", "-i", str(output),
             "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        ) if output.is_file() else None
        pixels = decoded.stdout if decoded and decoded.returncode == 0 else b""
        row = {
            "particles": count,
            "command": command,
            "elapsed_wall_seconds": elapsed,
            "exit_code": process.returncode,
            "backend": "vulkan / require_gpu_native",
            "output": str(output),
            "output_sha256": sha256(output) if output.is_file() else None,
            "decoded_exit_code": decoded.returncode if decoded else None,
            "decoded_rgb_sha256": sha256_bytes(pixels) if pixels else None,
            "decoded_rgb_mean": sum(pixels) / len(pixels) if pixels else None,
            "decoded_rgb_max": max(pixels) if pixels else None,
            "decoded_nonzero_channels": sum(value != 0 for value in pixels) if pixels else None,
            "decoded_channel_count": len(pixels),
            "log": str(log),
        }
        rows.append(row)
        print(json.dumps(row, sort_keys=True))

    RESULT.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    return 0 if all(row["exit_code"] == 0 and row["decoded_exit_code"] == 0
                    for row in rows) else 1


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


if __name__ == "__main__":
    raise SystemExit(main())
