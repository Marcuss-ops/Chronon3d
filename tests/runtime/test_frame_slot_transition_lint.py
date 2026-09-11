#!/usr/bin/env python3
"""Static guard for the single-writer frame-slot state machine."""

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
POOL_HEADER = ROOT / "include/chronon3d/runtime/frame/frame_slot_pool.hpp"
POOL_SOURCE = ROOT / "src/runtime/frame/frame_slot_pool.cpp"
JOB_HEADER = ROOT / "include/chronon3d/runtime/job/job_session.hpp"


def fail(message: str) -> None:
    print(f"frame_slot_transition_lint: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


if "set_state" in POOL_HEADER.read_text() or "set_state" in POOL_SOURCE.read_text():
    fail("legacy direct set_state writer still exists")

# Every direct store of FrameExecutionSlot::state must remain inside the pool
# authority. CAS transitions are intentionally allowed at the same boundary.
result = subprocess.run(
    ["rg", "-l", r"\.state\.store\(", "src", "include"],
    cwd=ROOT,
    check=False,
    text=True,
    capture_output=True,
)
writer_files = {
    Path(line).as_posix() for line in result.stdout.splitlines() if line.strip()
}
if writer_files - {POOL_SOURCE.relative_to(ROOT).as_posix()}:
    fail("FrameSlotState::state has a direct writer outside FrameSlotPool: "
         + ", ".join(sorted(writer_files)))

pool_source = POOL_SOURCE.read_text()
if len(re.findall(r"\.state\.compare_exchange_strong\(", pool_source)) != 2:
    fail("expected exactly acquire and transition CAS writers in FrameSlotPool")

job_source = JOB_HEADER.read_text()
if job_source.count("FrameSlotState::Evaluated,\n                                     FrameSlotState::Evaluating") != 1:
    fail("evaluated-queue rollback does not go through transition() exactly once")
if job_source.count("FrameSlotState::Rendered,\n                                     FrameSlotState::Evaluated") != 1:
    fail("rendered-queue rollback does not go through transition() exactly once")

print("frame_slot_transition_lint: PASS (single FrameSlotPool writer authority)")
