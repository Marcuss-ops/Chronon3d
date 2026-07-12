#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_chronon_product_linux.sh
#
# Canonical Chronon3D product certification orchestrator.
#
# Invokes all 12 canonical cert gates in sequence and emits
# `CHRONON_PRODUCT_FUNCTIONAL_PASS` only if ALL pass.
#
# Gate list (12 canonical + 2 forward-pointed = 14 total):
#   1.  verify_repository_baseline_linux  — repo health + clean build + 11/11 baseline
#   2.  verify_text_functional_linux       — Text V1 golden + preset + kinetic
#   3.  verify_camera_functional_linux     — Camera V1 runtime cert
#   4.  verify_render_runtime_linux        — 4 stills + 7 isolation
#   5.  verify_video_pipeline_linux        — 16 video combinations
#   6.  verify_asset_preflight_linux       — 10 sabotage scenarios
#   7.  verify_timeline_functional_linux   — global/local frame, boundary, nested, overlap
#   8.  verify_compositing_effects_linux   — 10 effects (opacity, blur, glow, shadow, etc.)
#   9.  verify_determinism_linux           — 4 invariants (same-process, separate, cache, order)
#   10. verify_error_handling_linux        — 10 error types (structured contract)
#   11. install_consumer_test              — external consumer SDK (find_package + build + run)
#   12. verify_packaging_linux             — relocatability (2 prefixes, mv, no abs paths)
#   13. verify_sanitizer_linux             — [forward-pointed] ASan/UBSan/TSan cert
#   14. verify_diagnostics_linux           — [forward-pointed] diagnostics pipeline cert
#
# Design (per AGENTS.md Cat-3 anti-dup):
#   - 1 canonical orchestrator (this file) — no per-category stub scripts.
#   - All sub-gates run even if prior ones fail, so the aggregate report
#     shows ALL verdicts in one invocation.
#   - Forward-pointed gates are documented here; they emit BLOCKED with
#     explicit ticket references until implemented.
#
# Per-gate contract (canonical exit codes):
#   0 = PASS (all checks succeed)
#   1 = FAIL (one or more checks fail)
#   2 = BLOCKED (env blocker per §honest-limitation)
#
# Unified verdict:
#   CHRONON_PRODUCT_FUNCTIONAL_PASS    — ALL gates returned 0
#   CHRONON_PRODUCT_FUNCTIONAL_FAIL    — ≥1 gate returned 1
#   CHRONON_PRODUCT_FUNCTIONAL_BLOCKED — ≥1 gate returned 2 + 0 FAILs
#
# Exit codes: 0 = PASS, 1 = FAIL, 2 = BLOCKED
#
# §honesty contract:
#   - BLOCKED sub-gates reported with explicit forward-point diagnostic
#   - [INFO] line on PASS only (per AGENTS.md §INFO-level diagnostic style)
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_chronon_product_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# NOTE: set -u + pipefail only — NO set -e (the orchestrator must run
# ALL sub-gates even if some fail, and aggregate the results at the end).
set -uo pipefail

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0

# Per-gate verdict log (machine-readable, one line per gate)
GATE_LOG="$(mktemp -t chronon3d_product_cert.XXXXXX.log)"
: > "$GATE_LOG"

# ── Helpers ──────────────────────────────────────────────────────────────────

# run_gate <gate-name> <script-path>
# Runs the sub-gate script, captures exit code, classifies verdict.
# Output is shown to the user AND appended to the log.
# Always returns 0 — the orchestrator does NOT abort on per-gate failure.
run_gate() {
    local name="$1" script="$2"

    if [ ! -f "$script" ]; then
        echo ""
        echo "=============================================="
        echo " [sub-gate] $name"
        echo "=============================================="
        echo "  [BLOCKED] script not found: $script"
        BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
        echo "BLOCKED $name (script not found)" >> "$GATE_LOG"
        return 0
    fi

    [ -x "$script" ] || chmod +x "$script" 2>/dev/null || true

    echo ""
    echo "=============================================="
    echo " [sub-gate] $name  ($script)"
    echo "=============================================="

    local sub_exit=0
    # Run the sub-gate. Output goes to terminal AND log.
    # Use ; not || so PIPESTATUS is always captured (not just on failure).
    bash "$script" 2>&1 | tee -a "$GATE_LOG"; sub_exit=${PIPESTATUS[0]}

    case "$sub_exit" in
        0)
            echo "  >> VERDICT: PASS ($name)"
            PASS_COUNT=$((PASS_COUNT + 1))
            echo "PASS $name" >> "$GATE_LOG"
            ;;
        1)
            echo "  >> VERDICT: FAIL ($name)"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "FAIL $name" >> "$GATE_LOG"
            ;;
        2)
            echo "  >> VERDICT: BLOCKED ($name — env blocker)"
            BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
            echo "BLOCKED $name" >> "$GATE_LOG"
            ;;
        *)
            echo "  >> VERDICT: FAIL ($name — unexpected exit $sub_exit)"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "FAIL $name (exit=$sub_exit)" >> "$GATE_LOG"
            ;;
    esac

    return 0
}

# forward_point_gate <gate-name> <ticket-ref> <description>
# Emits BLOCKED for gates not yet implemented, with explicit ticket reference.
forward_point_gate() {
    local name="$1" ticket="$2" desc="$3"

    echo ""
    echo "=============================================="
    echo " [sub-gate] $name"
    echo "=============================================="
    echo "  [BLOCKED] $name — not yet implemented"
    echo "             ticket: $ticket"
    echo "             $desc"
    BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
    echo "BLOCKED $name (forward-point: $ticket)" >> "$GATE_LOG"

    return 0
}

# ══════════════════════════════════════════════════════════════════════════════
# 0. Preamble
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_chronon_product_linux.sh"
echo " Chronon3D Unified Product Certification"
echo "=============================================="
echo ""
echo "  Repository: $ROOT"
echo "  HEAD:       $(git rev-parse --short HEAD)"
echo "  Branch:     $(git branch --show-current 2>/dev/null || echo 'detached')"
echo "  Started:    $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "  Gate log:   $GATE_LOG"
echo ""

# ══════════════════════════════════════════════════════════════════════════════
# 1–12. Canonical cert gates (sequential, all run regardless of prior failures)
# ══════════════════════════════════════════════════════════════════════════════

echo "== Orchestrating 12 canonical cert gates + 2 forward-pointed =="
echo "   (11 user-spec + 1 bonus: verify_error_handling_linux)"
echo "   (each gate runs to completion; aggregate verdict at end)"
echo ""

#  1 — Repository baseline
run_gate "verify_repository_baseline_linux" \
    "tools/verify_repository_baseline_linux.sh"

#  2 — Text V1
run_gate "verify_text_functional_linux" \
    "tools/verify_text_functional_linux.sh"

#  3 — Camera V1
run_gate "verify_camera_functional_linux" \
    "tools/verify_camera_functional_linux.sh"

#  4 — Render runtime
run_gate "verify_render_runtime_linux" \
    "tools/verify_render_runtime_linux.sh"

#  5 — Video pipeline
run_gate "verify_video_pipeline_linux" \
    "tools/verify_video_pipeline_linux.sh"

#  6 — Asset preflight
run_gate "verify_asset_preflight_linux" \
    "tools/verify_asset_preflight_linux.sh"

#  7 — Timeline & sequence
run_gate "verify_timeline_functional_linux" \
    "tools/verify_timeline_functional_linux.sh"

#  8 — Compositing & effects
run_gate "verify_compositing_effects_linux" \
    "tools/verify_compositing_effects_linux.sh"

#  9 — Determinism & cache
run_gate "verify_determinism_linux" \
    "tools/verify_determinism_linux.sh"

# 10 — Error handling & diagnostics
run_gate "verify_error_handling_linux" \
    "tools/verify_error_handling_linux.sh"

# 11 — External consumer SDK
run_gate "install_consumer_test" \
    "tools/install_consumer_test.sh"

# 12 — Packaging & relocatability
run_gate "verify_packaging_linux" \
    "tools/verify_packaging_linux.sh"

# ══════════════════════════════════════════════════════════════════════════════
# 13–14. Forward-pointed gates (not yet implemented)
# ══════════════════════════════════════════════════════════════════════════════

# 13 — Sanitizer (script exists as untracked; deferred until committed + verified)
if [ -f "tools/verify_sanitizer_linux.sh" ]; then
    run_gate "verify_sanitizer_linux" \
        "tools/verify_sanitizer_linux.sh"
else
    forward_point_gate "verify_sanitizer_linux" \
        "TICKET-VERIFY-SANITIZER-LINUX" \
        "ASan/UBSan/TSan cert gate (0 OOB / 0 UAF / 0 UB / 0 data races)."
fi

# 14 — Diagnostics pipeline
forward_point_gate "verify_diagnostics_linux" \
    "TICKET-VERIFY-DIAGNOSTICS-LINUX" \
    "Diagnostics pipeline cert (text_visibility_audit + render_diagnostic + audit snapshot)."

# ══════════════════════════════════════════════════════════════════════════════
# Final verdict
# ══════════════════════════════════════════════════════════════════════════════

TOTAL=$((PASS_COUNT + FAIL_COUNT + BLOCKED_COUNT))

echo ""
echo "=============================================="
echo " UNIFIED VERDICT"
echo "=============================================="
echo "  Total gates:  $TOTAL"
echo "  PASS:         $PASS_COUNT"
echo "  FAIL:         $FAIL_COUNT"
echo "  BLOCKED:      $BLOCKED_COUNT"
echo "  Finished:     $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "  Gate log:     $GATE_LOG"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "CHRONON_PRODUCT_FUNCTIONAL_FAIL"
    echo ""
    echo "  $FAIL_COUNT gate(s) FAILED out of $TOTAL total."
    echo "  See per-gate output above and $GATE_LOG for details."
    echo "  Per AGENTS.md §honesty: NO spurious PASS emitted."
    echo ""
    echo "  Failed gates:"
    grep -E '^FAIL ' "$GATE_LOG" 2>/dev/null || true
    exit 1

elif [ "$BLOCKED_COUNT" -gt 0 ]; then
    echo "CHRONON_PRODUCT_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  $BLOCKED_COUNT gate(s) BLOCKED out of $TOTAL total."
    echo "  0 gates FAILED — all executable gates passed."
    echo "  Blocked gates (env / forward-pointed):"
    grep -E '^BLOCKED ' "$GATE_LOG" 2>/dev/null || true
    echo ""
    echo "  Per AGENTS.md §honest-limitation: macchina-verifica DEFERRED"
    echo "  to working build host. Fix blockers and re-run."
    exit 2

else
    echo "CHRONON_PRODUCT_FUNCTIONAL_PASS"
    echo ""
    echo "  All $PASS_COUNT gates passed. Chronon3D product certified."
    echo ""
    echo "  Certified gates:"
    grep -E '^PASS ' "$GATE_LOG" 2>/dev/null || true
    echo ""
    echo "[INFO] ${GATE_NAME}: ${PASS_COUNT}/${TOTAL} gates PASS — Chronon3D product certification complete"
    exit 0
fi
