#!/usr/bin/env bash
#
# tools/regen_camera_golden.sh — explicit regeneration of the Step 5 golden.
#
# The Step 5 GOLDEN regression test in
#   tests/scene/camera/test_camera_program_compiled.cpp §10
# pins a uint64_t FNV-1a hash of the evaluated Camera2_5D for a fixed
# CameraDescriptor (`golden.trajectory_lens_dof`) using
# TrajectoryMotion + PhysicalLensProjection + DOF.  The hash is stored
# little-endian in
#   tests/scene/camera/_golden/trajectory_lens_dof.golden.bin
#
# ── REGEN PREREQUISITES ──────────────────────────────────────────────────
# Both must be TRUE on origin/main before this script can produce a
# non-sentinel hash:
#
#   (1) src/scene/camera/camera_v1/camera_program_compiler.cpp:330-335
#       changed from `traj->trajectory->size()` (segment count) to
#       `traj->trajectory->points().size()` (point count) for the
#       segment-index bound check.  Otherwise every 2-point / 1-segment
#       trajectory's compile_camera() rejects.
#
#   (2) tests/scene/camera/golden_projection_test.cpp's 4 references to
#       `FocalPx` (lines 136, 180, 229, 271) renamed to `FocalPixels`
#       or whatever the canonical type is now.  Otherwise the
#       scene-tests executable fails to link.
#
# ── USAGE ───────────────────────────────────────────────────────────────
#   bash tools/regen_camera_golden.sh
#
# The script ALWAYS re-captures the hash from a fresh doctest run, never
# relying on the .bin being the placeholder sentinel.  On the 2nd–Nth
# invocation the operator gets the same behaviour as the 1st — useful
# for drift detection after descriptor / engine changes.
#
# Post-conditions on success:
#   • tests/scene/camera/_golden/trajectory_lens_dof.golden.bin is
#     rewritten with 8-byte LE uint64_t of the freshly-captured hash.
#   • The .bin is `git add`-staged for the next commit.  Inspect with:
#       xxd tests/scene/camera/_golden/trajectory_lens_dof.golden.bin
#
# ── EXIT CODES ──────────────────────────────────────────────────────────
#   0  Hash captured and written to .bin successfully
#   1  Pre-existing upstream regression still blocking (printer-friendly
#      diagnostic names which of the two prerequisites is unmet)
#   2  Test executable not built or test filter did not match
#   3  Test ran but did not surface the hash via the doctest MESSAGE()
#      line (test compiled but evaluate() returned no .camera)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GOLDEN="${REPO_ROOT}/tests/scene/camera/_golden/trajectory_lens_dof.golden.bin"
TEST_EXE="${REPO_ROOT}/build/tests/chronon3d_scene_tests"

echo "── tools/regen_camera_golden.sh ─────────────────────────────────"
echo "repo:  ${REPO_ROOT}"
echo "golden: ${GOLDEN}"
echo

# ── 1. Probe upstream prerequisites first (no work, just diagnostic) ──
echo "[1/3] Probing upstream prerequisites on this checkout…"

# (a) trajectory-validator check: grep src for the bug signature.
if grep -nE 'const auto n_pts = traj->trajectory->size\(\)' \
    "${REPO_ROOT}/src/scene/camera/camera_v1/camera_program_compiler.cpp" \
    >/dev/null 2>&1; then
    echo "  FAIL prerequisite (1) STILL present:"
    echo "    src/scene/camera/camera_v1/camera_program_compiler.cpp still uses"
    echo "    'traj->trajectory->size()' as the segment-bound check. Fix: change"
    echo "    to 'traj->trajectory->points().size()' on the n_pts assignment."
    HAS_BLOCKER=1
else
    echo "  ok prerequisite (1): trajectory-validator size() → points().size()"
fi

# (b) golden_projection_test FocalPx check.
if [ -f "${REPO_ROOT}/tests/scene/camera/golden_projection_test.cpp" ] \
   && grep -nE 'FocalPx' \
       "${REPO_ROOT}/tests/scene/camera/golden_projection_test.cpp" \
       >/dev/null 2>&1; then
    echo "  FAIL prerequisite (2) STILL present:"
    echo "    tests/scene/camera/golden_projection_test.cpp still references"
    echo "    'FocalPx' (no longer a type). Fix: rename to the canonical type."
    HAS_BLOCKER=1
else
    echo "  ok prerequisite (2): no FocalPx references in golden_projection_test.cpp"
fi

if [ "${HAS_BLOCKER:-0}" -ne 0 ]; then
    echo
    echo "ABORT: prerequisite(s) unmet.  Re-run after both upstream fixes commit."
    exit 1
fi
echo

# ── 2. Locate the test executable ───────────────────────────────────────
echo "[2/3] Locating test executable…"
if [ ! -x "$TEST_EXE" ]; then
    echo "  not built at: $TEST_EXE"
    echo "  Build with:"
    echo "    cmake -S '$REPO_ROOT' -B '$REPO_ROOT/build'"
    echo "    cmake --build '$REPO_ROOT/build' --target chronon3d_scene_tests"
    exit 2
fi
echo "  using: $TEST_EXE"
echo

# ── 3. Run the §10 test, capture the hash MESSAGE(), write the .bin ────
echo "[3/3] Running §10 §1.C GOLDEN test to capture hash…"

# DESIGN NOTE — sentinel gate in the test:
#   The §10 TEST_CASE in test_camera_program_compiled.cpp only emits the
#   "first valid hash observed:" MESSAGE() line when the .bin is the
#   placeholder sentinel (0xDEADBEEFDEADBEEF).  After the first regen
#   the .bin holds a real hash, so subsequent runs would print no MESSAGE
#   and the script's `grep -E 'first valid hash observed:'` would match
#   nothing — exit 3 — even though the test still passes.
#
#   SCOPE OF THE FIX (choosing the script-level option per code-review):
#   Pre-emptively `rm -f "$GOLDEN"` so the test's sentinel-detection branch
#   fires on EVERY invocation.  The .bin is then rewritten with the
#   freshly-captured hash.  The committed-as-topic-of-record .bin stays
#   intact between regen invocations — only the script actively touches it.

if [ -f "$GOLDEN" ]; then
    # Defensive snapshot — leaves a `.bak` alongside the .bin so a future,
    # unforeseen regression that prevents this regen run from completing
    # doesn't strand the operator with no golden at all.
    #
    # We deliberately do NOT guard this with `|| true` and do NOT redirect
    # stderr.  With `set -e` active, a cp failure MUST abort the script so
    # the operator sees the diagnostic AND the original .bin remains intact
    # (well-formed SoftFail = forensic recovery; hidden fail = data loss).
    # Stderr is preserved so the diagnostic is visible.
    cp -f "$GOLDEN" "${GOLDEN}.bak"
    echo "  pre-flight: removing existing $GOLDEN to force sentinel"
    echo "             detection branch on this run."
    rm -f "$GOLDEN"
fi

# Require python3 for the binary write step (exit cleanly if absent).
command -v python3 >/dev/null 2>&1 || {
    echo "  python3 not found on PATH; cannot write 8-byte LE .bin."
    echo "  install python3 or invoke the script from an env that has it."
    exit 4
}

RUN_OUT="$(
    "$TEST_EXE" \
        --test-case='compiled_trajectory_lens_dof_golden' \
        --no-skip --duration=false 2>&1 || true
)"

HASH="$(printf '%s\n' "$RUN_OUT" \
    | grep -E 'first valid hash observed:[[:space:]]*[0-9]+' \
    | sed -E 's/.*first valid hash observed:[[:space:]]*([0-9]+).*/\1/' \
    | head -n1 || true)"

if [ -z "$HASH" ]; then
    echo "  hash MESSAGE line not found in test output."
    echo "  raw tail of test output (last 25 lines):"
    printf '%s\n' "$RUN_OUT" | tail -n 25
    exit 3
fi

if ! [[ "$HASH" =~ ^[0-9]+$ ]]; then
    echo "  captured hash is not numeric: $HASH"
    exit 3
fi

python3 -c "
import sys
v = int(sys.argv[1])
sys.stdout.buffer.write(v.to_bytes(8, 'little'))
" "$HASH" > "$GOLDEN"

echo "  captured hash:  $HASH"
echo "  wrote 8 bytes LE to: $GOLDEN"
echo "  (inspect with: xxd $GOLDEN)"
echo

if git -C "$REPO_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    if git -C "$REPO_ROOT" add "$GOLDEN" 2>&1; then
        echo "  git-staged for commit."
    else
        echo "  git add failed (file exists but git rejected it);"
        echo "  inspect manually with 'git status'."
    fi
else
    echo "  (not in a git worktree — CI / operator will pick this up directly.)"
fi

echo
echo "Done.  Inspect + commit the regenerated .bin."
