#!/usr/bin/env bash
# Phase 5 allocation gate. The benchmark harness writes one line named
# `steady_state_allocations_per_frame`; this gate refuses to infer a value from
# framebuffer allocations or from an absent report.
#
# Revision binding (2026-09-12, fixes.md B.4): the committed baseline is a
# historical artifact, so a PASS on it says nothing about the current revision.
# The gate therefore reads the report's `source_revision=` field and compares it
# with HEAD:
#   - bound    : report revision is a prefix of HEAD (or vice versa) → normal PASS
#   - mismatch : different revision → GATE_WARN, exit 0 (fatal with the strict env)
#   - unbound  : report has no `source_revision=` (or no local HEAD) → GATE_WARN
# The revision and its state are always printed, so a PASS can no longer be read
# as evidence about a revision it does not cover.
#
# Env:
#   CHRONON_STEADY_STATE_REPORT            report path (default: committed baseline)
#   CHRONON_STEADY_STATE_THRESHOLD         max allocations/frame (default: 0)
#   CHRONON_STEADY_STATE_REVISION          expected revision (default: git HEAD)
#   CHRONON_STEADY_STATE_REQUIRE_REVISION  "1" → a non-bound revision is fatal
#
# Exit codes: 0 = pass (possibly warned), 1 = fail, 2 = internal-script-error.
set -euo pipefail

GATE_NAME=steady_state_allocations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPORT="${CHRONON_STEADY_STATE_REPORT:-$REPO_ROOT/docs/baselines/steady-state-allocations-certified.txt}"
THRESHOLD="${CHRONON_STEADY_STATE_THRESHOLD:-0}"
REQUIRE_REVISION="${CHRONON_STEADY_STATE_REQUIRE_REVISION:-0}"

if [[ ! -s "$REPORT" ]]; then
    echo "GATE_FAIL_INTERNAL: missing allocation report: $REPORT" >&2
    echo "  run the P1E/Phase-5 harness and set CHRONON_STEADY_STATE_REPORT" >&2
    exit 2
fi

value="$(awk -F= '/^steady_state_allocations_per_frame=/ { gsub(/[[:space:]]/, "", $2); print $2; exit }' "$REPORT")"
if [[ -z "$value" ]]; then
    echo "GATE_FAIL_INTERNAL: report has no steady_state_allocations_per_frame=..." >&2
    exit 2
fi

# ── Revision binding ───────────────────────────────────────────────────────
report_revision="$(awk -F= '/^source_revision=/ { gsub(/[[:space:]]/, "", $2); print $2; exit }' "$REPORT" || true)"
if [[ -n "${CHRONON_STEADY_STATE_REVISION:-}" ]]; then
    expected_revision="$CHRONON_STEADY_STATE_REVISION"
else
    expected_revision="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || true)"
fi

revision_state="unbound"  # bound | mismatch | unbound
if [[ -n "$expected_revision" && -n "$report_revision" ]]; then
    if [[ "$expected_revision" == "$report_revision"* || "$report_revision" == "$expected_revision"* ]]; then
        revision_state="bound"
    else
        revision_state="mismatch"
    fi
fi

case "$revision_state" in
    bound)
        echo "GATE_INFO: report source_revision=$report_revision matches the expected revision"
        ;;
    mismatch)
        echo "GATE_WARN: report source_revision=$report_revision != expected ${expected_revision:0:12} — this PASS is historical, not evidence about the current revision" >&2
        ;;
    unbound)
        echo "GATE_WARN: no source_revision binding (report=${report_revision:-<absent>}, expected=${expected_revision:-<absent>}) — the value is not tied to a revision" >&2
        ;;
esac

if [[ "$revision_state" != "bound" && "$REQUIRE_REVISION" == "1" ]]; then
    echo "GATE_FAIL: $GATE_NAME revision_state=$revision_state report_source_revision=${report_revision:-<absent>} expected=${expected_revision:-<absent>} report=$REPORT (CHRONON_STEADY_STATE_REQUIRE_REVISION=1)" >&2
    exit 1
fi

if ! awk -v value="$value" -v threshold="$THRESHOLD" 'BEGIN { exit !(value >= 0 && value <= threshold) }'; then
    echo "GATE_FAIL: $GATE_NAME value=$value threshold=$THRESHOLD revision=${report_revision:-<absent>} report=$REPORT" >&2
    exit 1
fi

echo "GATE_PASS: $GATE_NAME steady_state_allocations_per_frame=$value threshold=$THRESHOLD revision=${report_revision:-<absent>} revision_state=$revision_state report=$REPORT"
