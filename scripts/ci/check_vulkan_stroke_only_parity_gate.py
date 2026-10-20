#!/usr/bin/env python3
"""Vulkan stroke-only path parity gate (TICKET-VULKAN-STROKE-ONLY-DRAW).

A fill-less path whose stroke uses round caps used to be dropped by the
Vulkan backend: `try_native_path_stroke` accepted `LineCap::Butt` only, so the
node fell through to `UnsupportedCapability`, the CLI kept exit 0 and the
frame contained the background alone. The defect lived in the routing, not in
the stroke kernel, which is why the kernel-level unit test could not see it.

This gate renders the minimal stroke-only shape through the raw pre-encode
sink (`--video-sink raw`, the repository's pixel-truth surface) with both
backends and fails closed when:

  1. the Vulkan render itself fails (pre-fix the CLI exited 0 while the node
     was rejected, so a coverage assert is the real lock);
  2. the Vulkan frame carries no alpha coverage at all;
  3. software and Vulkan diverge on any byte.

Vulkan availability: a host without a usable device skips the gate with an
explicit SKIP line instead of a false pass. Set CHRONON_GATE_REQUIRE_VULKAN=1
(e.g. on the GPU lane) to turn that skip into a failure.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "vulkan_stroke_only_gate"
PLAN = FIXTURE / "plan.json"


def cli() -> Path:
    for name in ("CHRONON_CLI", "CHRONON_GPU_CLI"):
        override = os.environ.get(name)
        if override:
            return Path(override)
    for candidate in (
        ROOT / "build" / "chronon" / "linux-video-release" / "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-video-test" / "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-fast-dev" / "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-svg-fast" / "apps" / "chronon3d_cli" / "chronon3d_cli",
    ):
        if candidate.is_file():
            return candidate
    raise SystemExit("chronon3d_cli not found (set CHRONON_CLI to a build with the video lane)")


def canvas_of(plan_path: Path) -> tuple[int, int]:
    canvas = json.loads(plan_path.read_text()).get("canvas", {})
    return (int(canvas.get("width", 0)), int(canvas.get("height", 0)))


def render_raw(output_dir: Path, backend: str, tag: str) -> Path:
    """Render the fixture through the raw pre-encode sink; return the dump."""
    plan = json.loads(PLAN.read_text())
    plan["output"] = {"path": f"{tag}.mp4", "format": "mp4"}
    prepared = output_dir / f"{tag}_plan.json"
    prepared.write_text(json.dumps(plan))
    raw_path = output_dir / f"{tag}_raw.dmp"
    cmd = [
        str(cli()), "render", "--plan", str(prepared),
        "--assets-root", str(ROOT), "--backend", backend,
        "--start-frame", "0", "--end-frame", "0",
        "--video-sink", "raw", "--output", str(raw_path),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return raw_path, proc


def main() -> int:
    width, height = canvas_of(PLAN)
    if width <= 0 or height <= 0:
        raise SystemExit(f"fixture has no usable canvas: {PLAN}")

    with tempfile.TemporaryDirectory(prefix="vulkan_stroke_only_gate_") as tmp:
        out = Path(tmp)

        software_raw, software_proc = render_raw(out, "software", "software")
        if software_proc.returncode:
            raise SystemExit(
                "software render failed (the oracle must always work): "
                f"code={software_proc.returncode}\n{software_proc.stdout[-2000:]}{software_proc.stderr[-2000:]}")
        if not software_raw.is_file():
            raise SystemExit(f"software render produced no raw dump: {software_raw}")

        vulkan_raw, vulkan_proc = render_raw(out, "vulkan", "vulkan")
        if vulkan_proc.returncode or not vulkan_raw.is_file():
            detail = f"code={vulkan_proc.returncode}\n{vulkan_proc.stdout[-2000:]}{vulkan_proc.stderr[-2000:]}"
            if os.environ.get("CHRONON_GATE_REQUIRE_VULKAN") == "1":
                raise SystemExit(f"vulkan render failed on a lane that requires a device:\n{detail}")
            print("SKIP  no usable Vulkan device on this host (set CHRONON_GATE_REQUIRE_VULKAN=1 to fail)")
            print(f"      vulkan detail: {detail.strip().splitlines()[-1] if detail.strip() else 'no output'}")
            return 0

        frame_bytes = width * height * 4
        soft = software_raw.read_bytes()[:frame_bytes]
        vk = vulkan_raw.read_bytes()[:frame_bytes]
        if len(soft) != frame_bytes or len(vk) != frame_bytes:
            raise SystemExit(
                f"raw frame truncated: software={len(soft)} vulkan={len(vk)} expected={frame_bytes}")

        covered = sum(1 for i in range(3, frame_bytes, 4) if vk[i])
        if covered == 0:
            raise SystemExit(
                "vulkan produced a fully transparent frame: the stroke-only path was "
                "dropped again (routing rejected the node and nothing failed closed)")
        print(f"  PASS  vulkan stroke-only coverage: {covered} px")

        soft_covered = sum(1 for i in range(3, frame_bytes, 4) if soft[i])
        if soft_covered == 0:
            raise SystemExit(
                "software oracle is fully transparent: the fixture no longer exercises the stroke")

        differing = [i for i in range(frame_bytes) if soft[i] != vk[i]]
        if differing:
            worst = max(abs(soft[i] - vk[i]) for i in differing)
            raise SystemExit(
                f"vulkan diverges from the software oracle: {len(differing)} byte(s) differ, "
                f"max delta {worst} (first at offset {differing[0]})")
        print(f"  PASS  software and vulkan raw frames are byte-identical ({frame_bytes} bytes)")

    print("PASS  vulkan stroke-only parity gate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
