#!/usr/bin/env python3
"""Lottie CLI frame-hash gates (TICKET-LOTTIE-ANIMATED).

Renders the L1 transform and L5 Trim Paths fixtures through the software
backend and asserts their authored keyframes decode to distinct frames. Fails
closed when the CLI is missing, either 10 MiB input bound is exceeded, or a
fixture's hashes collide.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "lottie_cli_gate"
PLAN = FIXTURE / "plan.json"
ANIM = FIXTURE / "mark_anim.json"
TRIM_PLAN = FIXTURE / "trim_plan.json"
TRIM_ANIM = FIXTURE / "trim_anim.json"
MAX_INPUT_BYTES = 10 * 1024 * 1024
KEYFRAMES = (0, 30)
FPS = 30.0


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    candidate = (ROOT / "build" / "chronon" / "linux-video-fast-dev" /
                 "apps" / "chronon3d_cli" / "chronon3d_cli")
    if candidate.is_file():
        return candidate
    raise SystemExit("chronon3d_cli not found (CHRONON_CLI or default build path)")


def frame_hash(video: Path, frame: int) -> str:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-ss", str(frame / FPS), "-i", str(video),
         "-vframes", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True)
    return hashlib.sha256(proc.stdout).hexdigest()


def main() -> int:
    if max(ANIM.stat().st_size, TRIM_ANIM.stat().st_size) > MAX_INPUT_BYTES:
        print(f"FAIL: fixture exceeds {MAX_INPUT_BYTES} bytes")
        return 1
    with tempfile.TemporaryDirectory(prefix="lottie_gate_") as tmp:
        cli_path = cli()
        for label, plan in (("L1", PLAN), ("L5 Trim Paths", TRIM_PLAN)):
            out = Path(tmp) / f"{label.replace(' ', '_')}.mp4"
            validate = subprocess.run(
                [str(cli_path), "validate", "--plan", str(plan),
                 "--assets-root", str(FIXTURE)], capture_output=True, text=True)
            if validate.returncode != 0:
                print(f"FAIL: {label} validate\n" + validate.stderr[-2000:])
                return 1
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(plan),
                 "--assets-root", str(FIXTURE), "--backend", "software",
                 "--output", str(out)], capture_output=True, text=True)
            if render.returncode != 0 or not out.is_file():
                print(f"FAIL: {label} render\n" + render.stderr[-2000:])
                return 1
            hashes = {f: frame_hash(out, f) for f in KEYFRAMES}
            for frame, digest in hashes.items():
                print(f"{label} frame {frame}: {digest}")
            if len(set(hashes.values())) != len(hashes):
                print(f"FAIL: {label} keyframe hashes collide")
                return 1
    print("LOTTIE_CLI_GATE PASS: L1 and animated Trim Paths frames differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
