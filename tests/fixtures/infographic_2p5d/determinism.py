#!/usr/bin/env python3
"""Prove what a render reproduces and what it does not.

Two lanes, two contracts:

  * `--video-sink raw` writes the pre-encode frames — the renderer's own
    output.  Its contract is *bit-exact* reproducibility.  NOTE: on the
    parallel (default) scheduler this lane currently FAILS that contract for a
    multi-layer scene; `CHRONON3D_SCHEDULER_WORKERS=1` restores it.  See the
    README section "Render determinism" for the measurements.
  * the mp4 lane is lossy, so its contract is structural: equal frame count and
    equal timestamps.  Byte-level equality is reported, not required — the
    encoder is entitled to finite precision.

Usage:
    python3 determinism.py [RUNS] [FRAMES] [--parallel]

  FRAMES defaults to `0-29`: the pre-encode lane writes ~2 MiB per frame at
  960x540 RGBA, so a full 60-frame clip costs ~120 MiB per run.
  By default the probe pins the render workers to 1 (the reproducibility
  setting); `--parallel` runs on the default worker count instead and is
  expected to FAIL, which is the reproduction of the defect.

Exit status is non-zero when a contract is broken.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from collections import Counter

CLI = "build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
PLAN = "tests/fixtures/infographic_2p5d/plans/20_scene.plan.json"
WORK = "/tmp/chronon_determinism"

args = [a for a in sys.argv[1:] if not a.startswith("--")]
parallel = "--parallel" in sys.argv
RUNS = int(args[0]) if args else 3
FRAMES = args[1] if len(args) > 1 else "0-29"
ENV = dict(os.environ)
if parallel:
    ENV.pop("CHRONON3D_SCHEDULER_WORKERS", None)
else:
    ENV["CHRONON3D_SCHEDULER_WORKERS"] = "1"


def render(out: str, *extra: str) -> None:
    subprocess.run(
        [CLI, "render", "--plan", PLAN, "-o", out, "--assets-root", os.getcwd(),
         "--backend", "software", "--frames", FRAMES, *extra],
        check=True, capture_output=True, env=ENV)


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ffprobe(path: str, entries: str) -> str:
    return subprocess.run(
        ["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
         "-show_entries", entries, "-of", "csv=p=0", path],
        check=True, capture_output=True, text=True).stdout.strip()


def main() -> int:
    os.makedirs(WORK, exist_ok=True)
    workers = ENV.get("CHRONON3D_SCHEDULER_WORKERS", "<default>")
    print(f"render workers: {workers}   runs: {RUNS}   frames: {FRAMES}")

    raw, mp4 = [], []
    for run in range(RUNS):
        raw_path = f"{WORK}/pre_encode_{run}.raw"
        mp4_path = f"{WORK}/encoded_{run}.mp4"
        render(raw_path, "--video-sink", "raw")
        render(mp4_path)
        raw.append(sha256(raw_path))
        mp4.append(sha256(mp4_path))

    failures = 0

    print("pre-encode lane (--video-sink raw):")
    for digest, count in Counter(raw).most_common():
        print(f"  {count}x {digest[:16]}")
    if len(set(raw)) != 1:
        print("  FAIL: the renderer's own frames differ between runs")
        failures += 1
    else:
        print("  PASS: bit-exact across runs")

    frames = [ffprobe(f"{WORK}/encoded_{i}.mp4", "stream=nb_read_frames")
              for i in range(RUNS)]
    stamps = [ffprobe(f"{WORK}/encoded_{i}.mp4", "frame=pts_time")
              for i in range(RUNS)]
    print(f"mp4 lane: {len(set(mp4))} distinct bitstreams of {RUNS}")
    if not frames[0].isdigit():
        print(f"  FAIL: ffprobe returned no frame count ({frames[0]!r})")
        failures += 1
    elif len(set(frames)) != 1:
        print(f"  FAIL: frame count varies: {frames}")
        failures += 1
    else:
        print(f"  PASS: frame count stable at {frames[0]}")
    if len(set(stamps)) != 1:
        print("  FAIL: frame timestamps vary between runs")
        failures += 1
    else:
        print("  PASS: frame timestamps stable")
    if len(set(mp4)) != 1:
        print("  NOTE: encoder output is not byte-reproducible here; compare "
              "decoded pixels with measure.py instead")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
