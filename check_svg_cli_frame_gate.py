#!/usr/bin/env python3
"""End-to-end SVG animation frame-hash gate.

Compiles a resolver-backed SVG through the canonical RenderPlan CLI, renders a short animated MP4 twice, and verifies visible, distinct,
repeatable decoded pixels at its opening and frozen endpoint.
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
KEYFRAMES = (0, 10)
MAX_INPUT_BYTES = 8 * 1024 * 1024


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    candidates = (
        ROOT / "build" / "chronon" / "svg-roadmap" /
        "apps" / "chronon3d_cli" / "chronon3d_cli",
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


def run_cli(command: str, *arguments: str) -> None:
    proc = subprocess.run([str(cli()), command, *arguments], capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"SVG CLI {command} failed:\n{proc.stderr[-3000:]}")


def render_endpoint_hashes(executable: Path, output_video: Path) -> dict[int, str]:
    proc = subprocess.run(
        [str(executable), "render", "--plan", str(PLAN), "--assets-root", str(FIXTURE),
         "--backend", "software", "--start-frame", "0", "--end-frame", str(max(KEYFRAMES)),
         "--output", str(output_video)], capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(f"SVG CLI render failed:\n{proc.stderr[-3000:]}")
    if not output_video.is_file():
        raise SystemExit(f"SVG CLI did not write the expected video: {output_video}")
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(output_video), "-f", "rawvideo",
         "-pix_fmt", "rgba", "-"], capture_output=True, check=True)
    frame_bytes = 64 * 64 * 4
    if len(proc.stdout) % frame_bytes:
        raise SystemExit("decoded render has a partial 64x64 RGBA frame")
    frames = [proc.stdout[offset:offset + frame_bytes]
              for offset in range(0, len(proc.stdout), frame_bytes)]
    if len(frames) != max(KEYFRAMES) + 1:
        raise SystemExit(f"render has {len(frames)} frames; expected {max(KEYFRAMES) + 1}")
    hashes = {}
    for frame in KEYFRAMES:
        pixels = frames[frame]
        if not any(pixels[index] or pixels[index + 1] or pixels[index + 2]
                   for index in range(0, len(pixels), 4)):
            raise SystemExit(f"frame {frame} is entirely black; no rendered control or SVG pixels")
        hashes[frame] = hashlib.sha256(pixels).hexdigest()
    return hashes


def main() -> int:
    if SVG.stat().st_size > MAX_INPUT_BYTES:
        raise SystemExit(f"fixture exceeds {MAX_INPUT_BYTES} bytes")
    executable = cli()
    with tempfile.TemporaryDirectory(prefix="svg_cli_gate_") as tmp:
        run_cli("validate", "--plan", str(PLAN), "--assets-root", str(FIXTURE))
        output_root = Path(tmp)
        first_run = render_endpoint_hashes(executable, output_root / "animated-first.mp4")
        hashes = render_endpoint_hashes(executable, output_root / "animated-second.mp4")
        if hashes != first_run:
            raise SystemExit(f"repeat SVG renders changed endpoint hashes: {first_run} != {hashes}")
    for frame, digest in hashes.items():
        print(f"SVG frame {frame}: {digest}")
    if len(set(hashes.values())) != len(hashes):
        raise SystemExit("SVG animation endpoint frame hashes collide")
    print("SVG_CLI_GATE PASS: endpoints differ and repeat-render hashes match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
