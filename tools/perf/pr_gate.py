#!/usr/bin/env python3
"""
PR Gate — Regression Test Suite for Chronon3D Performance PRs
=============================================================
Every performance PR must pass these 4 gates before merge:
  1. Determinism: same frame rendered twice = identical pixels
  2. Dirty-rect correctness: dirty rect on vs off = same pixels
  3. Golden frames: current render matches golden baselines
  4. Performance: FPS/memory within acceptable thresholds

Usage:
    # Full gate (requires baseline JSON for perf check)
    ./tools/perf/pr_gate.py --bin ./build/apps/chronon3d_cli/chronon3d_cli

    # With performance baseline
    ./tools/perf/pr_gate.py --bin ... --baseline reports/perf/breathing_best.json

    # Skip performance gate (e.g. first run, no baseline yet)
    ./tools/perf/pr_gate.py --bin ... --skip-perf

    # Only determinism + dirty rect (fast gate, ~10s)
    ./tools/perf/pr_gate.py --bin ... --fast

Returns exit code 0 on pass, non-zero on failure.
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ── Colours ────────────────────────────────────────────────────────────────
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
BOLD = "\033[1m"
RESET = "\033[0m"

PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
OUTPUT_DIR = PROJECT_DIR / "output" / "pr_gate"
GOLDEN_DIR = PROJECT_DIR / "tests" / "golden" / "breathing"
COMPOSITION = "MinimalistImageTrackingBreathing"
FRAMES = "0,1,30,75,149"


# ═══════════════════════════════════════════════════════════════════════════
# Helpers
# ═══════════════════════════════════════════════════════════════════════════

def run(args: List[str], timeout: int = 60) -> Tuple[int, str, str]:
    """Run a command, return (exit_code, stdout, stderr)."""
    p = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def file_hash(path: str) -> str:
    """SHA-256 hash of a file."""
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def png_pixels_identical(path1: str, path2: str) -> Tuple[bool, str]:
    """Check if two PNGs have identical pixel data (ignoring metadata)."""
    try:
        from PIL import Image, ImageChops
    except ImportError:
        # Fallback: compare raw file hashes
        h1, h2 = file_hash(path1), file_hash(path2)
        return h1 == h2, f"hash-based: {h1[:16]}... vs {h2[:16]}..."

    img1 = Image.open(path1).convert("RGBA")
    img2 = Image.open(path2).convert("RGBA")
    if img1.size != img2.size:
        return False, f"size mismatch: {img1.size} vs {img2.size}"

    diff = ImageChops.difference(img1, img2)
    bbox = diff.getbbox()
    if bbox is None:
        return True, "identical (no diff pixels)"

    # Count diff pixels
    diff_arr = list(diff.getdata())
    total = len(diff_arr)
    diff_count = sum(1 for p in diff_arr if p != (0, 0, 0, 0))
    ratio = diff_count / total * 100 if total > 0 else 0

    if ratio < 0.001:  # < 0.001% diff = acceptable subpixel variance
        return True, f"acceptable: {diff_count}/{total} diff pixels ({ratio:.4f}%)"
    return False, f"{diff_count}/{total} diff pixels ({ratio:.2f}%) — bbox={bbox}"


def rmse_between(path1: str, path2: str) -> float:
    """Root mean square error between two PNGs."""
    try:
        import numpy as np
        from PIL import Image
        img1 = np.array(Image.open(path1).convert("RGBA"), dtype=np.float64)
        img2 = np.array(Image.open(path2).convert("RGBA"), dtype=np.float64)
        if img1.shape != img2.shape:
            return 999.0
        diff = img1 - img2
        mse = np.mean(diff ** 2)
        return float(np.sqrt(mse))
    except ImportError:
        return 0.0 if file_hash(path1) == file_hash(path2) else 999.0


class GateResult:
    def __init__(self, name: str):
        self.name = name
        self.passed: Optional[bool] = None
        self.details: List[str] = []
        self.duration_ms: float = 0

    def ok(self, msg: str = ""):
        self.passed = True
        if msg:
            self.details.append(f"  {GREEN}✓{RESET} {msg}")

    def fail(self, msg: str):
        self.passed = False
        self.details.append(f"  {RED}✗{RESET} {msg}")

    def info(self, msg: str):
        self.details.append(f"  {msg}")

    def verdict(self) -> str:
        if self.passed is None:
            return f"{YELLOW}? SKIPPED{RESET}"
        if self.passed:
            return f"{GREEN}✓ PASS{RESET}"
        return f"{RED}✗ FAIL{RESET}"


# ═══════════════════════════════════════════════════════════════════════════
# Gate 1: Determinism
# ═══════════════════════════════════════════════════════════════════════════

def gate_determinism(bin_path: str) -> GateResult:
    """Render frame 75 twice, verify pixel-identical output."""
    g = GateResult("Determinism (frame 75 rendered 2×)")
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    t0 = time.time()

    p1 = str(OUTPUT_DIR / "det_a.png")
    p2 = str(OUTPUT_DIR / "det_b.png")

    g.info(f"Rendering {COMPOSITION} frame 75 (pass 1)...")
    rc, _, stderr = run([bin_path, "render", COMPOSITION, "--frames", "75", "-o", p1])
    if rc != 0:
        g.fail(f"Render pass 1 failed (exit {rc}): {stderr[:200]}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    g.info(f"Rendering {COMPOSITION} frame 75 (pass 2)...")
    rc, _, stderr = run([bin_path, "render", COMPOSITION, "--frames", "75", "-o", p2])
    if rc != 0:
        g.fail(f"Render pass 2 failed (exit {rc}): {stderr[:200]}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    ok, msg = png_pixels_identical(p1, p2)
    if ok:
        g.ok(f"Pixel-identical ({msg})")
        # Cleanup only on success
        for p in [p1, p2]:
            try:
                os.remove(p)
            except OSError:
                pass
    else:
        rmse = rmse_between(p1, p2)
        g.fail(f"Pixels differ! ({msg}, RMSE={rmse:.4f})")
        g.info(f"  Output files kept for inspection: {p1}, {p2}")

    g.duration_ms = (time.time() - t0) * 1000
    return g


# ═══════════════════════════════════════════════════════════════════════════
# Gate 2: Dirty Rect Correctness
# ═══════════════════════════════════════════════════════════════════════════

def gate_dirty_rect(bin_path: str) -> GateResult:
    """Render frame 75 with dirty rect ON and OFF, verify pixel-identical output."""
    g = GateResult("Dirty Rect Correctness (on vs off, frame 75)")
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    t0 = time.time()

    p_on = str(OUTPUT_DIR / "dirty_on.png")
    p_off = str(OUTPUT_DIR / "dirty_off.png")

    g.info(f"Rendering with --dirty-rects ON...")
    rc_on, _, stderr = run([
        bin_path, "render", COMPOSITION, "--frames", "75",
        "--dirty-rects", "-o", p_on
    ])
    if rc_on != 0:
        g.fail(f"Dirty-rect ON render failed (exit {rc_on}): {stderr[:200]}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    g.info(f"Rendering with --dirty-rects OFF...")
    rc_off, _, stderr = run([
        bin_path, "render", COMPOSITION, "--frames", "75",
        "-o", p_off
    ])
    if rc_off != 0:
        g.fail(f"Dirty-rect OFF render failed (exit {rc_off}): {stderr[:200]}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    ok, msg = png_pixels_identical(p_on, p_off)
    if ok:
        g.ok(f"Dirty rect produces identical pixels ({msg})")
    else:
        rmse = rmse_between(p_on, p_off)
        g.fail(f"Dirty rect introduces visual differences! ({msg}, RMSE={rmse:.4f})")
        g.info(f"  Output files kept for inspection: {p_on}, {p_off}")

    g.duration_ms = (time.time() - t0) * 1000
    return g


# ═══════════════════════════════════════════════════════════════════════════
# Gate 3: Golden Frames
# ═══════════════════════════════════════════════════════════════════════════

def gate_golden_frames(bin_path: str, accept_threshold: float = 0.5) -> GateResult:
    """Render golden frames and compare against baseline images."""
    g = GateResult(f"Golden Frames ({COMPOSITION} frames {FRAMES})")
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    t0 = time.time()

    if not GOLDEN_DIR.exists():
        g.fail(f"Golden directory not found: {GOLDEN_DIR}")
        g.info("  Generate golden frames first:")
        g.info(f"    python3 tools/perf/pr_gate.py --bin {bin_path} --generate-golden")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    frame_list = [int(x) for x in FRAMES.split(",")]
    all_ok = True

    for frame in frame_list:
        golden = GOLDEN_DIR / f"frame_{frame:03d}.png"
        current = OUTPUT_DIR / f"golden_current_{frame:03d}.png"

        if not golden.exists():
            g.fail(f"Golden for frame {frame} missing: {golden}")
            all_ok = False
            continue

        g.info(f"Frame {frame:3d}: rendering...")
        rc, _, stderr = run([
            bin_path, "render", COMPOSITION,
            "--frames", str(frame), "-o", str(current)
        ])
        if rc != 0:
            g.fail(f"Frame {frame} render failed (exit {rc}): {stderr[:200]}")
            all_ok = False
            continue

        ok, msg = png_pixels_identical(str(golden), str(current))
        if ok:
            g.ok(f"Frame {frame:3d}: identical")
        else:
            rmse = rmse_between(str(golden), str(current))
            if rmse <= accept_threshold:
                g.ok(f"Frame {frame:3d}: RMSE={rmse:.4f} (within threshold {accept_threshold})")
            else:
                g.fail(f"Frame {frame:3d}: RMSE={rmse:.4f} > {accept_threshold}! ({msg})")
                all_ok = False

    g.duration_ms = (time.time() - t0) * 1000
    return g


def generate_golden_frames(bin_path: str) -> bool:
    """Generate golden baseline frames."""
    os.makedirs(GOLDEN_DIR, exist_ok=True)
    print(f"Generating golden frames for {COMPOSITION}...")
    print(f"Output: {GOLDEN_DIR}")

    frame_list = [int(x) for x in FRAMES.split(",")]
    all_ok = True

    for frame in frame_list:
        output = GOLDEN_DIR / f"frame_{frame:03d}.png"
        print(f"  Frame {frame:3d} -> {output} ... ", end="", flush=True)
        rc, _, stderr = run([
            bin_path, "render", COMPOSITION,
            "--frames", str(frame), "-o", str(output)
        ])
        if rc != 0:
            print(f"FAILED (exit {rc})")
            print(f"    {stderr[:200]}")
            all_ok = False
        elif output.exists():
            size_kb = output.stat().st_size / 1024
            print(f"OK ({size_kb:.1f} KB)")
        else:
            print(f"MISSING (file not created)")
            all_ok = False

    if all_ok:
        print(f"\n✓ Golden frames generated successfully.")
        print(f"  Commit these files to the repository:")
        for frame in frame_list:
            print(f"    git add {GOLDEN_DIR / f'frame_{frame:03d}.png'}")
    return all_ok


# ═══════════════════════════════════════════════════════════════════════════
# Gate 4: Performance Regression
# ═══════════════════════════════════════════════════════════════════════════

def gate_performance(bin_path: str, baseline_json: str,
                     max_fps_regression: float = 0.05,
                     max_mem_regression: float = 0.10,
                     max_hotnode_regression: float = 0.10) -> GateResult:
    """Run breathing benchmark and compare against baseline."""
    g = GateResult("Performance Regression (breathing 150f vs baseline)")
    t0 = time.time()

    # Ensure baseline exists
    if not baseline_json or not os.path.exists(baseline_json):
        g.fail(f"Baseline JSON not found: {baseline_json}")
        g.info("  Create one with:")
        g.info("    python3 tools/perf/compare_telemetry.py <baseline_run_id> ... --json-out reports/perf/breathing_best.json")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    try:
        with open(baseline_json) as f:
            baseline_data = json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        g.fail(f"Cannot read baseline JSON: {e}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    # Extract baseline metrics
    if "current" in baseline_data:
        baseline = baseline_data["current"].get("metrics", {})
    elif "baseline" in baseline_data:
        baseline = baseline_data["baseline"].get("metrics", {})
    else:
        # Raw dump format
        baseline = baseline_data
        if "_counters" in baseline:
            for k, v in baseline["_counters"].items():
                baseline[k] = v
            del baseline["_counters"]

    # Run benchmark
    g.info(f"Running breathing benchmark (150 frames @ 30fps)...")
    bench_start = time.time()
    mp4 = str(OUTPUT_DIR / "pr_gate_bench.mp4")
    rc, stdout, stderr = run([
        bin_path, "video", COMPOSITION,
        "--start", "0", "--end", "150", "--fps", "30",
        "-o", mp4,
    ], timeout=120)
    bench_duration = time.time() - bench_start

    if rc != 0:
        g.fail(f"Benchmark failed (exit {rc}): {stderr[:300]}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    # Extract current metrics from telemetry DB
    try:
        import sqlite3
        db_path = Path.home() / ".chronon3d" / "telemetry" / "chronon3d_render_history.sqlite"
        db = sqlite3.connect(str(db_path))
        row = db.execute(
            "SELECT run_id, effective_fps, wall_time_ms, render_ms, bytes_allocated_peak, "
            "framebuffer_bytes_peak FROM render_runs "
            "WHERE composition_id=? ORDER BY finished_at_iso DESC LIMIT 1",
            (COMPOSITION,)
        ).fetchone()
        current_run_id = row[0]
        counters = {}
        for c in db.execute(
            "SELECT counter_name, counter_value FROM render_counters WHERE run_id=?",
            (current_run_id,)
        ):
            counters[c[0]] = c[1]
        db.close()
    except Exception as e:
        g.fail(f"Cannot read telemetry: {e}")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    if not row or not row[1]:
        g.fail("No telemetry data found for current run")
        g.duration_ms = (time.time() - t0) * 1000
        return g

    current_fps = row[1]
    current_wall = row[2]
    current_render = row[3]
    current_mem = row[4] / (1024 * 1024) if row[4] else 0
    current_fb = row[5] / (1024 * 1024) if row[5] else 0
    current_restore = counters.get("clearnode_restore_ms", 0)
    current_clear = counters.get("clearnode_clear_ms", 0)
    current_conversion = counters.get("frame_conversion_copy_ms", 0)
    current_composite = counters.get("composite_ms", 0)
    current_transform = counters.get("transform_ms", 0)

    baseline_fps = baseline.get("effective_fps", 0)
    baseline_mem = baseline.get("peak_memory_mb", 0)
    baseline_restore = baseline.get("clearnode_restore_ms", 0)
    baseline_clear = baseline.get("clearnode_clear_ms", 0)
    baseline_conversion = baseline.get("frame_conversion_copy_ms", 0)
    baseline_composite = baseline.get("composite_ms", 0)
    baseline_transform = baseline.get("transform_ms", 0)

    def pct_change(old, new):
        if old == 0:
            return 0.0
        return (new - old) / old

    all_ok = True

    # FPS (higher is better, so regression = negative change)
    fps_delta = pct_change(baseline_fps, current_fps)
    if fps_delta < -max_fps_regression:
        g.fail(f"FPS: {baseline_fps:.2f} → {current_fps:.2f} ({fps_delta*100:+.1f}% > -{max_fps_regression*100:.0f}%)")
        all_ok = False
    else:
        g.ok(f"FPS: {baseline_fps:.2f} → {current_fps:.2f} ({fps_delta*100:+.1f}%)")

    # Memory (lower is better)
    mem_delta = pct_change(baseline_mem, current_mem)
    if mem_delta > max_mem_regression:
        g.fail(f"Peak Memory: {baseline_mem:.1f} → {current_mem:.1f} MB ({mem_delta*100:+.1f}% > +{max_mem_regression*100:.0f}%)")
        all_ok = False
    else:
        g.ok(f"Peak Memory: {baseline_mem:.1f} → {current_mem:.1f} MB ({mem_delta*100:+.1f}%)")

    # Hot nodes
    for name, bl, cur in [
        ("ClearNode restore", baseline_restore, current_restore),
        ("ClearNode clear", baseline_clear, current_clear),
        ("Frame conversion", baseline_conversion, current_conversion),
        ("Composite", baseline_composite, current_composite),
        ("Transform", baseline_transform, current_transform),
    ]:
        delta = pct_change(bl, cur)
        if delta > max_hotnode_regression:
            g.fail(f"{name}: {bl:.0f} → {cur:.0f} ms ({delta*100:+.1f}% > +{max_hotnode_regression*100:.0f}%)")
            all_ok = False

    if all_ok:
        g.info(f"  All metrics within thresholds (bench took {bench_duration:.1f}s)")

    g.duration_ms = (time.time() - t0) * 1000
    return g


# ═══════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="PR Gate — Regression test suite for Chronon3D performance PRs",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--bin", required=True, help="Path to chronon3d_cli binary")
    parser.add_argument("--baseline", help="Path to baseline JSON (for perf comparison)")
    parser.add_argument("--skip-perf", action="store_true", help="Skip performance gate")
    parser.add_argument("--skip-golden", action="store_true", help="Skip golden frame gate")
    parser.add_argument("--fast", action="store_true",
                        help="Fast gate: only determinism + dirty rect (~10s)")
    parser.add_argument("--generate-golden", action="store_true",
                        help="Generate golden baseline frames (run once, then commit)")
    parser.add_argument("--max-fps-regression", type=float, default=0.05,
                        help="Max FPS regression (default: 0.05 = 5%%)")
    parser.add_argument("--max-mem-regression", type=float, default=0.10,
                        help="Max memory regression (default: 0.10 = 10%%)")
    parser.add_argument("--max-hotnode-regression", type=float, default=0.10,
                        help="Max hot-node time regression (default: 0.10 = 10%%)")
    parser.add_argument("--golden-rmse-threshold", type=float, default=0.5,
                        help="Max RMSE for golden frame acceptance (default: 0.5)")

    args = parser.parse_args()

    bin_path = args.bin
    if not os.path.isfile(bin_path) or not os.access(bin_path, os.X_OK):
        print(f"Error: binary not found or not executable: {bin_path}", file=sys.stderr)
        sys.exit(1)

    # ── Generate golden frames mode ──────────────────────────────────────
    if args.generate_golden:
        ok = generate_golden_frames(bin_path)
        sys.exit(0 if ok else 1)

    # ── Run gates ────────────────────────────────────────────────────────
    print(f"\n{'═' * 72}")
    print(f"  PR GATE — Regression Test Suite")
    print(f"  Binary: {bin_path}")
    print(f"  Composition: {COMPOSITION}")
    print(f"{'═' * 72}\n")

    gates: List[GateResult] = []

    # Gate 1: Determinism
    g1 = gate_determinism(bin_path)
    gates.append(g1)

    # Gate 2: Dirty Rect
    g2 = gate_dirty_rect(bin_path)
    gates.append(g2)

    if not args.fast:
        # Gate 3: Golden Frames
        if not args.skip_golden:
            g3 = gate_golden_frames(bin_path, args.golden_rmse_threshold)
            gates.append(g3)

        # Gate 4: Performance
        if not args.skip_perf:
            g4 = gate_performance(
                bin_path,
                args.baseline or "",
                args.max_fps_regression,
                args.max_mem_regression,
                args.max_hotnode_regression,
            )
            gates.append(g4)

    # ── Final report ─────────────────────────────────────────────────────
    print(f"\n{'═' * 72}")
    print(f"  RESULTS")
    print(f"{'═' * 72}")

    all_pass = True
    for g in gates:
        verdict = g.verdict()
        duration = f"({g.duration_ms/1000:.1f}s)"
        print(f"\n  [{verdict}] {g.name} {duration}")
        for d in g.details:
            print(d)
        if g.passed is False:
            all_pass = False

    print(f"\n{'═' * 72}")
    if all_pass:
        print(f"  {GREEN}{BOLD}ALL GATES PASSED — Safe to merge{RESET}")
        print(f"{'═' * 72}\n")
        sys.exit(0)
    else:
        failed = sum(1 for g in gates if g.passed is False)
        print(f"  {RED}{BOLD}{failed} GATE(S) FAILED — Do NOT merge{RESET}")
        print(f"{'═' * 72}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
