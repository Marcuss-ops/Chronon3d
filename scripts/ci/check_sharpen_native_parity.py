#!/usr/bin/env python3
"""Sharpen native-GPU parity gate (TICKET-VULKAN-EFFECT-PARITY, wave 2).

Oracle methodology (isolates the kernel from transport/encode noise):
- CPU oracle: `--backend software`, libx264 pipe, CRF 0 (mathematically
  lossless) — exact CPU-effect pixels.
- Native lane: `--backend vulkan --gpu-hot-path-mode require_gpu_native`
  (CPU fallback is refused, so success proves the native sharpen kernel ran),
  NVENC native, QP 0.
Frame 15 is extracted with accurate seek and compared in 8-bit: the residual
floor is float summation order (CPU sliding window vs GPU per-pixel box sums,
amplified by the unsharp gain on high-frequency content) plus 8-bit/NV12
rounding. Bounds: mean abs diff <= 2.5 and max abs diff <= 25. The no-effect
transport floor on this fixture is mean 0.75 / max 11.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "sharpen_native_parity"
PLAN = FIXTURE / "plan.json"
FRAME = 15
MEAN_BOUND = 2.5
MAX_BOUND = 25

SW_ARGS = ["--backend", "software", "--hardware", "none",
           "--encoder-backend", "pipe", "--rate-control", "crf", "--crf", "0"]
VK_ARGS = ["--backend", "vulkan", "--gpu-hot-path-mode", "require_gpu_native",
           "--hardware", "nvenc", "--encoder-backend", "native",
           "--rate-control", "qp", "--qp", "0"]


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    candidate = (ROOT / "build" / "chronon" / "linux-video-fast-dev" /
                 "apps" / "chronon3d_cli" / "chronon3d_cli")
    if candidate.is_file():
        return candidate
    raise SystemExit("chronon3d_cli not found (CHRONON_CLI or default build path)")


def render(args: list[str], out: Path) -> None:
    proc = subprocess.run([str(cli()), "render", "--plan", str(PLAN),
                           "--assets-root", str(FIXTURE), *args,
                           "--output", str(out)],
                          capture_output=True, text=True)
    if proc.returncode != 0 or not out.is_file():
        print("FAIL: render " + " ".join(args) + "\n" + proc.stderr[-2000:])
        raise SystemExit(1)


def grab(video: Path, out: Path) -> None:
    subprocess.run(["ffmpeg", "-v", "error", "-i", str(video),
                    "-vf", f"select=eq(n\\,{FRAME})", "-vframes", "1",
                    str(out)], check=True)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="sharpen_gate_") as tmp:
        tmpdir = Path(tmp)
        sw_mp4, vk_mp4 = tmpdir / "sw.mp4", tmpdir / "vk.mp4"
        render(SW_ARGS, sw_mp4)
        render(VK_ARGS, vk_mp4)
        sw_png, vk_png = tmpdir / "sw.png", tmpdir / "vk.png"
        grab(sw_mp4, sw_png)
        grab(vk_mp4, vk_png)
        a = np.asarray(Image.open(sw_png).convert("RGB"), dtype=np.int16)
        b = np.asarray(Image.open(vk_png).convert("RGB"), dtype=np.int16)
        if a.shape != b.shape:
            print(f"FAIL: shape mismatch {a.shape} vs {b.shape}")
            return 1
        diff = np.abs(a - b)
        mean, maximum = float(diff.mean()), int(diff.max())
        print(f"mean abs diff: {mean:.3f} (bound {MEAN_BOUND})")
        print(f"max abs diff: {maximum} (bound {MAX_BOUND})")
        if mean > MEAN_BOUND or maximum > MAX_BOUND:
            print("FAIL: parity bounds exceeded")
            return 1
    print("SHARPEN_PARITY_GATE PASS: native kernel matches the CPU oracle")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
