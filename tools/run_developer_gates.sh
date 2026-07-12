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

echo "run_developer_gates.sh: starting developer gate chain (9 gates)..."

# ── Gate 1: test hygiene (duplicate DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN) ──
echo "  [1/9] check_test_hygiene.sh"
bash "${SCRIPT_DIR}/check_test_hygiene.sh" \
    || { echo "GATE_FAIL: check_test_hygiene.sh (exit $?)" >&2; exit 1; }

# ── Gate 2: test suite registration (raw add_executable audit) ──────────
echo "  [2/9] check_test_suite_registration.sh"
bash "${SCRIPT_DIR}/check_test_suite_registration.sh" \
    || { echo "GATE_FAIL: check_test_suite_registration.sh (exit $?)" >&2; exit 1; }

# ── Gate 3: Frame::value canonical-reading convention ────────────────────
echo "  [3/9] check_frame_value_convention.sh"
bash "${SCRIPT_DIR}/check_frame_value_convention.sh" \
    || { echo "GATE_FAIL: check_frame_value_convention.sh (exit $?)" >&2; exit 1; }

# ── Gate 4: CHANGELOG conflict-marker invariant ──────────────────────────
echo "  [4/9] check_no_changelog_conflict_markers.sh"
bash "${SCRIPT_DIR}/check_no_changelog_conflict_markers.sh" \
    || { echo "GATE_FAIL: check_no_changelog_conflict_markers.sh (exit $?)" >&2; exit 1; }

# ── Gate 5: TextMultilingual source registration alignment ───────────────
echo "  [5/9] check_text_golden_sources_aligned.sh"
bash "${SCRIPT_DIR}/check_text_golden_sources_aligned.sh" \
    || { echo "GATE_FAIL: check_text_golden_sources_aligned.sh (exit $?)" >&2; exit 1; }

# ── Gate 6: docs/adr SHA-cite dedup ─────────────────────────────────────
echo "  [6/9] check_doc_sha_dedup.sh"
bash "${SCRIPT_DIR}/check_doc_sha_dedup.sh" \
    || { echo "GATE_FAIL: check_doc_sha_dedup.sh (exit $?)" >&2; exit 1; }

# ── Gate 7: 72-char commit-subject envelope ─────────────────────────────
echo "  [7/9] check_commit_subject_length.sh ${COMPARISON_REF}"
bash "${SCRIPT_DIR}/check_commit_subject_length.sh" "${COMPARISON_REF}" \
    || { echo "GATE_FAIL: check_commit_subject_length.sh (exit $?)" >&2; exit 1; }

# ── Gate 8: divergence-window advisory ──────────────────────────────────
echo "  [8/9] check_push_divergence_window.sh ${TARGET_REMOTE} ${TARGET_BRANCH}"
bash "${SCRIPT_DIR}/check_push_divergence_window.sh" "${TARGET_REMOTE}" "${TARGET_BRANCH}" \
    || { echo "GATE_FAIL: check_push_divergence_window.sh (exit $?)" >&2; exit 1; }

# ── Gate 9: architecture boundaries (deny-everywhere patterns) ──────────
echo "  [9/9] check_architecture_boundaries.sh"
bash "${SCRIPT_DIR}/check_architecture_boundaries.sh" \
    || { echo "GATE_FAIL: check_architecture_boundaries.sh (exit $?)" >&2; exit 1; }

echo "run_developer_gates.sh: 9/9 developer gates PASSED"
exit 0
