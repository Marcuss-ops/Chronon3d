#!/usr/bin/env python3
"""Run five strict-native caster-off and caster-on renders and summarize pixels."""

import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OFF = ROOT / "docs/evidence/lights-shadows/T05-native-vulkan-20261028/caster-off-control.plan.json"
ON = ROOT / "docs/evidence/lights-shadows/T05-c3d-production-20261022/t05-production.plan.json"
RESULT = HERE / "acceptance-results.json"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"usage: {sys.argv[0]} /path/to/chronon3d_cli [tag]", file=sys.stderr)
        return 2
    cli = Path(sys.argv[1]).resolve()
    tag = sys.argv[2] if len(sys.argv) == 3 else "debug"
    result_path = HERE / f"acceptance-results-{tag}.json"
    if not cli.is_file():
        print(f"CLI not found: {cli}", file=sys.stderr)
        return 2

    rows = []
    for label, plan in (("off", OFF), ("on", ON)):
        for run in range(1, 6):
            stem = f"acceptance-{tag}-{label}-{run:02d}"
            output = HERE / f"{stem}.mp4"
            log = HERE / f"{stem}.log"
            command = [
                str(cli), "render", "--plan", str(plan),
                "--start-frame", "0", "--end-frame", "0",
                "--output", str(output), "--assets-root", str(ROOT),
                "--backend", "vulkan", "--gpu-hot-path-mode", "require_gpu_native",
                "--hardware", "nvenc", "--encoder-backend", "native",
                "--log-level", "info",
            ]
            started = time.perf_counter()
            with log.open("wb") as stream:
                process = subprocess.run(command, cwd=ROOT, stdout=stream,
                                         stderr=subprocess.STDOUT, check=False)
            elapsed = time.perf_counter() - started
            raw = subprocess.run(
                ["ffmpeg", "-y", "-v", "error", "-i", str(output),
                 "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            ) if output.is_file() else None
            pixels = raw.stdout if raw and raw.returncode == 0 else b""
            row = {
                "mode": label,
                "run": run,
                "command": command,
                "render_wall_seconds": elapsed,
                "render_exit_code": process.returncode,
                "output_sha256": digest(output.read_bytes()) if output.is_file() else None,
                "decoded_exit_code": raw.returncode if raw else None,
                "decoded_rgb_sha256": digest(pixels) if pixels else None,
                "decoded_rgb_mean": sum(pixels) / len(pixels) if pixels else None,
                "decoded_rgb_max": max(pixels) if pixels else None,
                "decoded_nonzero_channels": sum(value != 0 for value in pixels) if pixels else None,
                "decoded_channel_count": len(pixels),
                "log": str(log),
                "output": str(output),
            }
            rows.append(row)
            print(json.dumps(row, sort_keys=True))

    result_path.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    return 0 if all(row["render_exit_code"] == 0 and
                    row["decoded_exit_code"] == 0 for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
