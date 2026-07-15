#!/usr/bin/env bash
# Canonical Chronon3D product certification orchestrator.
#
# Runs every product gate, never aborts on the first sub-gate failure, and
# emits one aggregate PASS/FAIL/BLOCKED verdict. There are no forward-pointed
# entries: every row is an executable script path.
#
# Exit codes:
#   0 = CHRONON_PRODUCT_FUNCTIONAL_PASS
#   1 = CHRONON_PRODUCT_FUNCTIONAL_FAIL
#   2 = CHRONON_PRODUCT_FUNCTIONAL_BLOCKED

set -uo pipefail

GATE_NAME="verify_chronon_product_linux"
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT" || {
    echo "CHRONON_PRODUCT_FUNCTIONAL_BLOCKED"
    echo "  cannot enter repository root: $ROOT"
    exit 2
}

GATE_LOG="${CHRONON3D_PRODUCT_GATE_LOG:-/tmp/chronon3d_product_cert.log}"
: > "$GATE_LOG"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0

GATE_NAMES=(
    verify_repository_baseline_linux
    verify_text_functional_linux
    verify_camera_functional_linux
    verify_render_runtime_linux
    verify_video_pipeline_linux
    verify_asset_preflight_linux
    verify_timeline_functional_linux
    verify_compositing_effects_linux
    verify_determinism_linux
    verify_error_handling_linux
    install_consumer_test
    verify_packaging_linux
    verify_performance_linux
    verify_sanitizer_linux
    verify_diagnostics_linux
)

GATE_SCRIPTS=(
    tools/verify_repository_baseline_linux.sh
    tools/verify_text_functional_linux.sh
    tools/verify_camera_functional_linux.sh
    tools/verify_render_runtime_linux.sh
    tools/verify_video_pipeline_linux.sh
    tools/verify_asset_preflight_linux.sh
    tools/verify_timeline_functional_linux.sh
    tools/verify_compositing_effects_linux.sh
    tools/verify_determinism_linux.sh
    tools/verify_error_handling_linux.sh
    tools/install_consumer_test.sh
    tools/verify_packaging_linux.sh
    tools/verify_performance_linux.sh
    tools/verify_sanitizer_linux.sh
    tools/verify_diagnostics_linux.sh
)

if [ "${#GATE_NAMES[@]}" -ne "${#GATE_SCRIPTS[@]}" ]; then
    echo "CHRONON_PRODUCT_FUNCTIONAL_BLOCKED"
    echo "  internal gate manifest mismatch"
    exit 2
fi

run_gate() {
    local name="$1"
    local script="$2"

    echo ""
    echo "=============================================="
    echo " [sub-gate] $name  ($script)"
    echo "=============================================="

    if [ ! -f "$script" ]; then
        echo "  [BLOCKED] script not found: $script"
        echo "BLOCKED $name (script not found)" >> "$GATE_LOG"
        BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
        return 0
    fi

    local sub_exit=0
    bash "$script" 2>&1 | tee -a "$GATE_LOG"
    sub_exit=${PIPESTATUS[0]}

    case "$sub_exit" in
        0)
            echo "  >> VERDICT: PASS ($name)"
            echo "PASS $name" >> "$GATE_LOG"
            PASS_COUNT=$((PASS_COUNT + 1))
            ;;
        1)
            echo "  >> VERDICT: FAIL ($name)"
            echo "FAIL $name" >> "$GATE_LOG"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            ;;
        2)
            echo "  >> VERDICT: BLOCKED ($name)"
            echo "BLOCKED $name" >> "$GATE_LOG"
            BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
            ;;
        *)
            echo "  >> VERDICT: FAIL ($name — unexpected exit $sub_exit)"
            echo "FAIL $name (exit=$sub_exit)" >> "$GATE_LOG"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            ;;
    esac
}

echo "=============================================="
echo " verify_chronon_product_linux.sh"
echo " Chronon3D Unified Product Certification"
echo "=============================================="
echo ""
echo "  Repository: $ROOT"
echo "  HEAD:       $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "  Branch:     $(git branch --show-current 2>/dev/null || echo detached)"
echo "  Started:    $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "  Gate log:   $GATE_LOG"
echo "  Gate count: ${#GATE_NAMES[@]} executable"

for index in "${!GATE_NAMES[@]}"; do
    run_gate "${GATE_NAMES[$index]}" "${GATE_SCRIPTS[$index]}"
done

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
    echo "  $FAIL_COUNT gate(s) failed out of $TOTAL."
    grep -E '^FAIL ' "$GATE_LOG" 2>/dev/null || true
    exit 1
fi

if [ "$BLOCKED_COUNT" -gt 0 ]; then
    echo "CHRONON_PRODUCT_FUNCTIONAL_BLOCKED"
    echo "  $BLOCKED_COUNT gate(s) blocked out of $TOTAL; no failures observed."
    grep -E '^BLOCKED ' "$GATE_LOG" 2>/dev/null || true
    exit 2
fi

echo "CHRONON_PRODUCT_FUNCTIONAL_PASS"
echo "  All $PASS_COUNT executable gates passed on the same commit."
grep -E '^PASS ' "$GATE_LOG" 2>/dev/null || true
echo "[INFO] ${GATE_NAME}: ${PASS_COUNT}/${TOTAL} executable gates PASS"
exit 0
