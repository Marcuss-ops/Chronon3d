#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# run_developer_gates.sh — canonical developer gate chain (9 gates).
#
# Called by:
#   - tools/wrap_push.sh (post-fetch, post-auto-FF, post-check_main_clean)
#   - .githooks/pre-push     (git push pre-push hook)
#
# Single source of truth: the gate list lives HERE, not duplicated across
# the wrapper and the hook.  All 9 gates are local-only, fast, and safe on
# any push (no MP4/build artifacts required).
#
# Usage:
#   bash tools/run_developer_gates.sh [remote] [branch]
#
#   remote  — default: origin
#   branch  — default: current branch (git rev-parse --abbrev-ref HEAD)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="${REPO_ROOT}/tools"
TARGET_REMOTE="${1:-origin}"
TARGET_BRANCH="${2:-$(git rev-parse --abbrev-ref HEAD)}"
COMPARISON_REF="${TARGET_REMOTE}/${TARGET_BRANCH}"

# Load the canonical gate manifest.
# shellcheck source=gates/manifest.sh
source "${SCRIPT_DIR}/gates/manifest.sh"

GATE_COUNT=${#DEVELOPER_GATES[@]}
echo "run_developer_gates.sh: starting developer gate chain (${GATE_COUNT} gates)..."

idx=0
for gate in "${DEVELOPER_GATES[@]}"; do
    idx=$((idx + 1))
    echo "  [${idx}/${GATE_COUNT}] ${gate}"

    case "$gate" in
        check_commit_subject_length.sh)
            bash "${SCRIPT_DIR}/${gate}" "${COMPARISON_REF}" \
                || { echo "GATE_FAIL: ${gate} (exit $?)" >&2; exit 1; }
            ;;
        check_push_divergence_window.sh)
            bash "${SCRIPT_DIR}/${gate}" "${TARGET_REMOTE}" "${TARGET_BRANCH}" \
                || { echo "GATE_FAIL: ${gate} (exit $?)" >&2; exit 1; }
            ;;
        *)
            bash "${SCRIPT_DIR}/${gate}" \
                || { echo "GATE_FAIL: ${gate} (exit $?)" >&2; exit 1; }
            ;;
    esac
done

echo "run_developer_gates.sh: ${GATE_COUNT}/${GATE_COUNT} developer gates PASSED"
exit 0
