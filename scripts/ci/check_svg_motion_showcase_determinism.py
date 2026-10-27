#!/usr/bin/env python3
"""Render and pixel-compare the complete SVG motion showcase twice."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_BASE = ROOT / "tests/fixtures/svg_motion_showcase/scene.plan.base.json"
PLAN_GENERATOR = ROOT / "tests/fixtures/svg_motion_showcase/generate_plan.py"
FRAME_BYTES = 1920 * 1080 * 4


def run_checked(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        tail = (result.stdout + "\n" + result.stderr)[-8000:]
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{tail}")
    return result


def inspect_video(ffprobe: str, path: Path) -> dict[str, object]:
    result = run_checked([
        ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,avg_frame_rate,nb_read_frames",
        "-show_entries", "format=duration", "-of", "json", str(path),
    ], cwd=ROOT)
    data = json.loads(result.stdout)
    stream = data["streams"][0]
    actual = {
        "width": int(stream["width"]),
        "height": int(stream["height"]),
        "fps": stream["avg_frame_rate"],
        "frames": int(stream["nb_read_frames"]),
        "duration": float(data["format"]["duration"]),
    }
    expected = {"width": 1920, "height": 1080, "fps": "30/1", "frames": 360}
    for key, value in expected.items():
        if actual[key] != value:
            raise RuntimeError(f"{path.name}: expected {key}={value}, got {actual[key]}")
    if abs(float(actual["duration"]) - 12.0) > 0.001:
        raise RuntimeError(f"{path.name}: expected 12.000s duration, got {actual['duration']}")
    return actual


def decoded_frame_hashes(ffmpeg: str, path: Path) -> list[str]:
    process = subprocess.Popen([
        ffmpeg, "-v", "error", "-i", str(path), "-f", "rawvideo",
        "-pix_fmt", "rgba", "pipe:1",
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert process.stdout is not None
    hashes: list[str] = []
    while True:
        chunks = bytearray()
        while len(chunks) < FRAME_BYTES:
            part = process.stdout.read(FRAME_BYTES - len(chunks))
            if not part:
                break
            chunks.extend(part)
        if not chunks:
            break
        if len(chunks) != FRAME_BYTES:
            process.kill()
            raise RuntimeError(f"{path.name}: truncated decoded frame {len(hashes)}")
        hashes.append(hashlib.sha256(chunks).hexdigest())
    stderr = process.stderr.read().decode("utf-8", errors="replace") if process.stderr else ""
    if process.wait() != 0:
        raise RuntimeError(f"FFmpeg could not decode {path.name}: {stderr[-4000:]}")
    return hashes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path, help="built chronon3d_cli executable")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--assets-root", type=Path, default=ROOT)
    args = parser.parse_args()
    cli = args.cli.resolve()
    assets_root = args.assets_root.resolve()
    if not cli.is_file():
        parser.error(f"Chronon3D CLI does not exist: {cli}")
    if not PLAN_BASE.is_file() or not PLAN_GENERATOR.is_file():
        parser.error(f"showcase plan source or generator is missing: {PLAN_BASE}")

    with tempfile.TemporaryDirectory(prefix="chronon-svg-showcase-") as temporary:
        plan_path = Path(temporary) / "scene.plan.json"
        run_checked([sys.executable, str(PLAN_GENERATOR), str(PLAN_BASE), str(plan_path)], cwd=ROOT)
        outputs = [Path(temporary) / f"showcase-{index}.mp4" for index in (1, 2)]
        for output in outputs:
            command = [
                str(cli), "render", "--plan", str(plan_path), "--assets-root",
                str(assets_root), "--backend", "software", "--encoder-backend",
                "pipe", "--output", str(output),
            ]
            print(f"Rendering {output.name} with the in-process FFmpeg pipe encoder…", flush=True)
            run_checked(command, cwd=ROOT)

        metadata = [inspect_video(args.ffprobe, output) for output in outputs]
        hashes = [decoded_frame_hashes(args.ffmpeg, output) for output in outputs]
        if len(hashes[0]) != 360 or len(hashes[1]) != 360:
            raise RuntimeError(f"expected 360 decoded frames, got {len(hashes[0])} and {len(hashes[1])}")
        differences = [index for index, pair in enumerate(zip(*hashes)) if pair[0] != pair[1]]
        if differences:
            raise RuntimeError(
                f"decoded showcase pixels differ in {len(differences)} frames; "
                f"first={differences[0]}, last={differences[-1]}"
            )
        files_identical = hashlib.sha256(outputs[0].read_bytes()).digest() == hashlib.sha256(outputs[1].read_bytes()).digest()
        print(f"PASS: {metadata[0]}; all 360 decoded RGBA frames match.")
        print(f"MP4 bitstreams identical: {'yes' if files_identical else 'no'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, KeyError, ValueError, json.JSONDecodeError) as error:
        print(f"SVG showcase determinism gate failed: {error}", file=sys.stderr)
        raise SystemExit(1)
