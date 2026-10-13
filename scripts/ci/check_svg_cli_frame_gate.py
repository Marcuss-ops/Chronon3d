#!/usr/bin/env python3
"""End-to-end SVG animation frame-hash gate.

Compiles a resolver-backed SVG asset through the canonical RenderPlan CLI,
renders with the software backend, and verifies the authored endpoints differ.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "svg_cli_gate"
PLAN = FIXTURE / "plan.json"
SVG = FIXTURE / "animated.svg"
KEYFRAMES = (0, 30)
MAX_INPUT_BYTES = 8 * 1024 * 1024


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    candidates = (
        ROOT / "build" / "chronon" / "linux-video-test" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-fast-dev" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-ci-full-validation" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("chronon3d_cli not found (set CHRONON_CLI to an SVG-enabled build)")


def frame_hashes(video: Path) -> dict[int, str]:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(video), "-f", "rawvideo",
         "-pix_fmt", "rgba", "-"], capture_output=True, check=True)
    frame_bytes = 64 * 64 * 4
    if len(proc.stdout) % frame_bytes:
        raise SystemExit("decoded SVG render has a partial RGBA frame")
    frames = [proc.stdout[offset:offset + frame_bytes]
              for offset in range(0, len(proc.stdout), frame_bytes)]
    if max(KEYFRAMES) >= len(frames):
        raise SystemExit(f"render has {len(frames)} frames; expected frame {max(KEYFRAMES)}")
    return {frame: hashlib.sha256(frames[frame]).hexdigest() for frame in KEYFRAMES}


def run_cli(command: str, *arguments: str) -> None:
    proc = subprocess.run([str(cli()), command, *arguments], capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"SVG CLI {command} failed:\n{proc.stderr[-3000:]}")


def main() -> int:
    if SVG.stat().st_size > MAX_INPUT_BYTES:
        raise SystemExit(f"fixture exceeds {MAX_INPUT_BYTES} bytes")
    executable = cli()
    with tempfile.TemporaryDirectory(prefix="svg_cli_gate_") as tmp:
        run_cli("validate", "--plan", str(PLAN), "--assets-root", str(FIXTURE))
        output_pattern = Path(tmp) / "animated_####.png"
        proc = subprocess.run(
            [str(executable), "render", "--plan", str(PLAN), "--assets-root", str(FIXTURE),
             "--backend", "software", "--start-frame", "0", "--end-frame", "30",
             "--output", str(output_pattern)], capture_output=True, text=True)
        if proc.returncode:
            raise SystemExit(f"SVG CLI render failed:\n{proc.stderr[-3000:]}")
        frame_paths = {frame: Path(tmp) / f"animated_{frame:04d}.png"
                       for frame in KEYFRAMES}
        missing = [str(path) for path in frame_paths.values() if not path.is_file()]
        if missing:
            raise SystemExit(f"SVG CLI did not write expected frames: {missing}")
        hashes = {}
        for frame, path in frame_paths.items():
            proc = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path),
                                   "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                                  capture_output=True, check=True)
            if len(proc.stdout) != 64 * 64 * 4:
                raise SystemExit(f"frame {frame} did not decode to one 64x64 RGBA image")
            if not any(proc.stdout[3::4]):
                raise SystemExit(f"frame {frame} is fully transparent")
            hashes[frame] = hashlib.sha256(proc.stdout).hexdigest()
    for frame, digest in hashes.items():
        print(f"SVG frame {frame}: {digest}")
    if len(set(hashes.values())) != len(hashes):
        raise SystemExit("SVG animation endpoint frame hashes collide")
    print("SVG_CLI_GATE PASS: animated SVG endpoint frames differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
