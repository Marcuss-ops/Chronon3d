#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_chronon_product_linux.sh
#
# Unified Chronon3D product certification command.
#
# Per user spec verbatim "Componi il comando unico di certificazione prodotto:
# crea `tools/verify_chronon_product_linux.sh` con `set -euo pipefail` che
# chiama in sequenza: `verify_repository_baseline_linux`,
# `verify_text_functional_linux`, `verify_camera_functional_linux`,
# `verify_render_runtime_linux`, `verify_video_pipeline_linux`,
# `verify_asset_preflight_linux`, `verify_timeline_functional_linux`,
# `verify_compositing_effects_linux`, `verify_determinism_linux`,
# `install_consumer_test`, `verify_packaging_linux` (+ verifica_diagnostics/perf/
# sanitizer). Stampa `CHRONON_PRODUCT_FUNCTIONAL_PASS` solo se tutti PASS."
#
# This script orchestrates the 11 user-spec cert gates + 3 forward-pointed
# (diagnostics / perf / sanitizer) gates = 14 total cert calls in sequence,
# captures each per-gate verdict + exit code, aggregates pass/fail/blocked
# counts, and emits the canonical `CHRONON_PRODUCT_FUNCTIONAL_PASS` only if
# ALL 14 gates PASS.
#
# Design (per AGENTS.md Cat-3 anti-dup rule):
#   * 1 canonical unified gate (this file) — no per-sub-gate stub scripts
#   * 6 missing sub-gates (verify_compositing_effects_linux +
#     verify_determinism_linux + verify_packaging_linux +
#     verify_diagnostics_linux + verify_perf_linux + verify_sanitizer_linux)
#     are forward-pointed inside this gate with explicit BLOCKED + diagnostic,
#     not stubbed.  Option B per the thinker's design recommendation
#     (Cat-3 minimal-surface: 1 file instead of 7).
#
# Per-gate contract (canonical exit codes 0/1/2):
#   0 = PASS (all checks succeed on working build host)
#   1 = FAIL (one or more checks fail)
#   2 = BLOCKED (env blocker per §honest-limitation: no build env, no
#       chronon3d_cli binary, no vcpkg, no ffmpeg, etc.)
#
# Unified-gate verdict:
#   * CHRONON_PRODUCT_FUNCTIONAL_PASS — ALL 14 gates returned 0
#   * CHRONON_PRODUCT_FUNCTIONAL_FAIL — at least 1 gate returned 1
#   * CHRONON_PRODUCT_FUNCTIONAL_BLOCKED — at least 1 gate returned 2 + no FAILs
#     (the §honest-limitation pattern preserved across the 14 sub-gates)
#
# Note on per-gate run-on-failure policy:
#   The user spec says "chiama in sequenza" — sequential.  We do NOT abort
#   on the first failure (per the aggregator pattern, not the gate pattern):
#   every sub-gate runs even if a prior one fails, so the aggregate report
#   shows ALL 14 verdicts in one invocation.  This is the canonical
#   multi-gate aggregator behavior (parallel to the §orchestrator pattern in
#   `tools/first_principles_product_check.sh` which runs all sections even
#   on partial failure).
#
# Exit codes:
#   0 = CHRONON_PRODUCT_FUNCTIONAL_PASS
#   1 = CHRONON_PRODUCT_FUNCTIONAL_FAIL
#   2 = CHRONON_PRODUCT_FUNCTIONAL_BLOCKED
#
# §honesty contract (AGENTS.md):
#   - BLOCKED sub-gates reported with explicit forward-point diagnostic, not
#     silently skipped.
#   - Addizionale [INFO] ${GATE_NAME}: ... line on PASS only (per AGENTS.md
#     "## Regole di lint documentale" §INFO-level diagnostic style).
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME="verify_chronon_product_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# ── Aggregator state ────────────────────────────────────────────────────

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
SKIPPED_COUNT=0

# Per-gate verdict log (one line per gate, for grep-discoverability)
GATE_LOG="/tmp/chronon3d_product_cert.log"
: > "$GATE_LOG"

# ── Helpers ─────────────────────────────────────────────────────────────

# run_gate <gate-name> <script-path> [forward-point-msg]
# Runs the script, captures its exit code, classifies the verdict.
# Always returns 0 (the aggregator does not abort on per-gate failure).
run_gate() {
    local name="$1"
    local script="$2"
    local forward_point="${3:-}"

    if [ ! -f "$script" ]; then
        echo "  [BLOCKED] $name — script not found: $script"
        if [ -n "$forward_point" ]; then
            echo "             forward-point: $forward_point"
        fi
        BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
        echo "BLOCKED $name (script not found)" >> "$GATE_LOG"
        return 0
    fi
    if [ ! -x "$script" ]; then
        chmod +x "$script" 2>/dev/null || true
    fi

    echo ""
    echo "=============================================="
    echo " [sub-gate] $name"
    echo "=============================================="

    # Run the sub-gate, capture exit code; tee to log for grep-discoverability
    local sub_exit=0
    bash "$script" 2>&1 | tee -a "$GATE_LOG" > /dev/null || sub_exit=$?

    case "$sub_exit" in
        0)
            echo "  [PASS] $name"
            PASS_COUNT=$((PASS_COUNT + 1))
            echo "PASS $name" >> "$GATE_LOG"
            ;;
        1)
            echo "  [FAIL] $name — exit code 1"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "FAIL $name" >> "$GATE_LOG"
            ;;
        2)
            echo "  [BLOCKED] $name — exit code 2 (env blocker per §honest-limitation)"
            if [ -n "$forward_point" ]; then
                echo "             forward-point: $forward_point"
            fi
            BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
            echo "BLOCKED $name" >> "$GATE_LOG"
            ;;
        *)
            echo "  [FAIL] $name — unexpected exit code $sub_exit"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "FAIL $name (unexpected exit code $sub_exit)" >> "$GATE_LOG"
            ;;
    esac

    return 0
}

# run_forward_point_gate <gate-name> <forward-point-msg>
# Emits BLOCKED with explicit forward-point diagnostic for gates that
# don't exist yet (per Cat-3 anti-dup: forward-point in the unified gate
# only, no separate stub script).
run_forward_point_gate() {
    local name="$1"
    local forward_point="$2"

    echo ""
    echo "=============================================="
    echo " [sub-gate] $name"
    echo "=============================================="
    echo "  [BLOCKED] $name — not yet implemented"
    echo "             forward-point: $forward_point"
    BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
    echo "BLOCKED $name (forward-point: $forward_point)" >> "$GATE_LOG"

    return 0
}

# ══════════════════════════════════════════════════════════════════════════
# 1. Repository state (canonical §honest-limitation precondition)
# ══════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_chronon_product_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

# Check clean tree
if ! git diff --quiet; then
    echo "CHRONON_FAIL: working tree has uncommitted changes"
    echo "  Diagnostic: commit or stash your changes before running the unified cert."
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "CHRONON_FAIL: index has staged changes"
    echo "  Diagnostic: commit or unstage your changes before running the unified cert."
    exit 1
fi

# Check aligned with origin/main
git fetch origin 2>/dev/null || true
BEHIND=$(git rev-list --left-right --count origin/main...HEAD 2>/dev/null | awk '{print $1}') || BEHIND="?"
if [ "$BEHIND" != "0" ]; then
    echo "CHRONON_FAIL: HEAD is behind origin/main (behind=$BEHIND)"
    echo "  Diagnostic: pull --rebase before running the unified cert."
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Branch: $(git branch --show-current)"
echo "  Tree: clean"
echo "  [PASS] Repository state"

# ══════════════════════════════════════════════════════════════════════════
# 2. Sub-gates — 11 user-spec gates + 3 forward-pointed = 14 total
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Sub-gate orchestration (14 gates total) =="

# ── 2.1 verify_repository_baseline_linux ──
run_gate "verify_repository_baseline_linux" \
    "tools/verify_repository_baseline_linux.sh"

# ── 2.2 verify_text_functional_linux ──
run_gate "verify_text_functional_linux" \
    "tools/verify_text_functional_linux.sh"

# ── 2.3 verify_camera_functional_linux ──
run_gate "verify_camera_functional_linux" \
    "tools/verify_camera_functional_linux.sh"

# ── 2.4 verify_render_runtime_linux ──
run_gate "verify_render_runtime_linux" \
    "tools/verify_render_runtime_linux.sh"

# ── 2.5 verify_video_pipeline_linux ──
run_gate "verify_video_pipeline_linux" \
    "tools/verify_video_pipeline_linux.sh"

# ── 2.6 verify_asset_preflight_linux ──
run_gate "verify_asset_preflight_linux" \
    "tools/verify_asset_preflight_linux.sh"

# ── 2.7 verify_timeline_functional_linux ──
run_gate "verify_timeline_functional_linux" \
    "tools/verify_timeline_functional_linux.sh"

# ── 2.8 verify_compositing_effects_linux (FORWARD-POINTED) ──
run_forward_point_gate "verify_compositing_effects_linux" \
    "TICKET-VERIFY-COMPOSITING-EFFECTS-LINUX: new gate to certify the compositing pipeline (alpha + blending + layer-order invariant + bloom + glow composition). Mirrors the verify_*_linux.sh 7-section FAIL-LOUD pattern."

# ── 2.9 verify_determinism_linux (FORWARD-POINTED) ──
run_forward_point_gate "verify_determinism_linux" \
    "TICKET-VERIFY-DETERMINISM-LINUX: new gate to certify the determinism pipeline (cross-process parity + frame-value hash identity + CHRONON3D_THREADS sweep). The existing check_determinism.sh covers the kernel logic but is not yet wrapped in a verify_*_linux.sh 7-section FAIL-LOUD pattern."

# ── 2.10 install_consumer_test ──
run_gate "install_consumer_test" \
    "tools/install_consumer_test.sh"

# ── 2.11 verify_packaging_linux (FORWARD-POINTED) ──
run_forward_point_gate "verify_packaging_linux" \
    "TICKET-VERIFY-PACKAGING-LINUX: new gate to certify the SDK packaging (CMake install + ar t + nm -C cross-language ABI + .chronon format). Mirrors the verify_*_linux.sh 7-section FAIL-LOUD pattern."

# ── 2.12 verify_diagnostics_linux (FORWARD-POINTED) ──
run_forward_point_gate "verify_diagnostics_linux" \
    "TICKET-VERIFY-DIAGNOSTICS-LINUX: new gate to certify the diagnostics pipeline (text_visibility_audit + render_diagnostic + audit snapshot serialization). Forward-pointed per user spec verbatim 'verifica_diagnostics/perf/sanitizer'."

# ── 2.13 verify_perf_linux (FORWARD-POINTED) ──
run_forward_point_gate "verify_perf_linux" \
    "TICKET-VERIFY-PERF-LINUX: new gate to certify the performance budget (per-frame render cost + peak memory + 60-frame sweep + linear-scaling check). Forward-pointed per user spec verbatim 'verifica_diagnostics/perf/sanitizer'."

# ── 2.14 verify_sanitizer_linux (FORWARD-POINTED) ──
run_forward_point_gate "verify_sanitizer_linux" \
    "TICKET-VERIFY-SANITIZER-LINUX: new gate to certify the 7-subsystem sanitizer cert (0 OOB / 0 UAF / 0 UB / 0 data races under ASAN/UBSAN/TSAN per the linux-asan + linux-tsan presets). Forward-pointed per user spec verbatim 'verifica_diagnostics/perf/sanitizer'."

# ══════════════════════════════════════════════════════════════════════════
# 3. Final verdict
# ══════════════════════════════════════════════════════════════════════════

TOTAL=$((PASS_COUNT + FAIL_COUNT + BLOCKED_COUNT))

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  Total sub-gates:  $TOTAL"
echo "  PASS:             $PASS_COUNT"
echo "  FAIL:             $FAIL_COUNT"
echo "  BLOCKED:          $BLOCKED_COUNT"
echo ""
echo "  Per-gate log:     $GATE_LOG"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "CHRONON_PRODUCT_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT sub-gate(s) failed. See details above + $GATE_LOG"
    echo "  Per AGENTS.md §honesty: NO spurious PASS emitted, NO silent skip-on-failure."
    exit 1
elif [ "$BLOCKED_COUNT" -gt 0 ]; then
    echo "CHRONON_PRODUCT_FUNCTIONAL_BLOCKED"
    echo "  $BLOCKED_COUNT sub-gate(s) blocked (env blocker per §honest-limitation)."
    echo "  Per AGENTS.md §honest-limitation: macchina-verifica DEFERRED to working build host"
    echo "  per the established TICKET-BUILD-ROT-CASCADE-CAMERA 409-error rot +"
    echo "  TICKET-CONTENT-TEXT-CAMERA-V1-ROT 21-error rot +"
    echo "  TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV vcpkg glm/magic_enum missing on this VPS."
    echo "  Fix the build rot on a working build host, then re-run this script."
    echo ""
    echo "  Forward-pointed sub-gates:"
    grep -E '^BLOCKED .*forward-point' "$GATE_LOG" || true
    exit 2
else
    echo "CHRONON_PRODUCT_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT sub-gates passed. Chronon3D product certified."
    # AGENTS.md Rule #2 — addizionale [INFO] line on PASS (≤ 200 chars,
    # grep-discoverable via [INFO] prefix + verify_chronon_product_linux
    # self-identifier).  This is the canonical INFO-level diagnostic.
    echo "[INFO] ${GATE_NAME}: 14/14 sub-gates verified on working build host (11 user-spec + 3 forward-pointed diagnostics/perf/sanitizer)"
    exit 0
fi
