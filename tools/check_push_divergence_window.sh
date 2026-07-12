#!/usr/bin/env bash
# check_push_divergence_window.sh - ADR-022 advisory gate
# See docs/adr/ADR-022-divergence-window-gate.md for full spec.

GATE_NAME=check_push_divergence_window

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "${REPO_ROOT}"

REMOTE="${1:-origin}"
BRANCH="${2:-$(git rev-parse --abbrev-ref HEAD)}"

MAX_LOCAL_AHEAD="${CHRONON3D_DIV_WINDOW_MAX_LOCAL_AHEAD:-10}"
MAX_REMOTE_AHEAD="${CHRONON3D_DIV_WINDOW_MAX_REMOTE_AHEAD:-10}"

if ! git fetch "${REMOTE}" 2>/dev/null; then
    echo "GATE_FAIL_INTERNAL: ${GATE_NAME}: git fetch ${REMOTE} failed" >&2
    exit 2
fi

REMOTE_REF="${REMOTE}/${BRANCH}"
LOCAL_AHEAD="$(git rev-list --count "${REMOTE_REF}..HEAD" 2>/dev/null || echo 0)"
REMOTE_AHEAD="$(git rev-list --count "HEAD..${REMOTE_REF}" 2>/dev/null || echo 0)"

LOCAL_ANC_OF_REMOTE="NO"
REMOTE_ANC_OF_LOCAL="NO"
if git merge-base --is-ancestor HEAD "${REMOTE_REF}" 2>/dev/null; then
    LOCAL_ANC_OF_REMOTE="YES"
fi
if git merge-base --is-ancestor "${REMOTE_REF}" HEAD 2>/dev/null; then
    REMOTE_ANC_OF_LOCAL="YES"
fi

IS_TRUE_DIVERGENCE="NO"
if [ "${LOCAL_ANC_OF_REMOTE}" = "NO" ] && [ "${REMOTE_ANC_OF_LOCAL}" = "NO" ]; then
    IS_TRUE_DIVERGENCE="YES"
fi

echo "[INFO] ${GATE_NAME}: LOCAL_AHEAD=${LOCAL_AHEAD} REMOTE_AHEAD=${REMOTE_AHEAD} true_divergence=${IS_TRUE_DIVERGENCE} thresholds LOCAL=${MAX_LOCAL_AHEAD} REMOTE=${MAX_REMOTE_AHEAD}"

if [ "${MAX_LOCAL_AHEAD}" -gt 0 ] && [ "${LOCAL_AHEAD}" -gt "${MAX_LOCAL_AHEAD}" ]; then
    echo "[WARN] ${GATE_NAME}: LOCAL_AHEAD=${LOCAL_AHEAD} exceeds threshold ${MAX_LOCAL_AHEAD}" >&2
fi
if [ "${MAX_REMOTE_AHEAD}" -gt 0 ] && [ "${REMOTE_AHEAD}" -gt "${MAX_REMOTE_AHEAD}" ]; then
    echo "[WARN] ${GATE_NAME}: REMOTE_AHEAD=${REMOTE_AHEAD} exceeds threshold ${MAX_REMOTE_AHEAD}" >&2
fi
if [ "${IS_TRUE_DIVERGENCE}" = "YES" ]; then
    echo "[WARN] ${GATE_NAME}: TRUE divergence; existing tools/check_main_clean.sh would hard-block this at Step 4" >&2
fi

exit 0
