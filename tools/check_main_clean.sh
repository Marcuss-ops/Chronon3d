#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_main_clean.sh — GATE-MNT-01: main-sync fail-on-dirty check
# ═══════════════════════════════════════════════════════════════════════════
#
# Verifies that the local checkout is in lockstep with origin/main and has
# no uncommitted / untracked changes.  This is the canonical machine-check
# of the GATE-MNT-01 invariant that all atomic-commit workflows (see
# AGENTS.md "Workflow Git obbligatorio") must satisfy before pushing.
#
# Behaviour (expressly per user spec, 2026-06-29):
#   git fetch origin
#   test "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)"
#   [ -z "$(git status -s)" ]
# All three must hold; otherwise GATE_FAIL (exit 1) is emitted with
# diagnostics on stderr.
#
# On success: stdout "GATE_PASS", exit 0.
# On failure: stdout "GATE_FAIL", exit 1, diagnostic on stderr.
#
# Idempotent:
#   - safe to run repeatedly
#   - safe to invoke from a post-rebase hook context (the fetch is a no-op
#     if origin/main is already up-to-date)
#   - safe to invoke from CI runners (treats dirty state the same way)
#
# Usage:
#   tools/check_main_clean.sh                         # exit-code check
#   GATE=$(tools/check_main_clean.sh && echo PASS || echo FAIL)
#
# Invoked by:
#   - tools/wrap_push.sh (canonical, repo-tracked, all Agent3 atomic commits)
#   - .git/hooks/pre-push  (defence-in-depth on raw `git push` calls)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

# 1. Bring remote refs up to date.
if ! git fetch origin 2>/dev/null; then
    echo "GATE_FAIL: git fetch origin failed" >&2
    echo "GATE_FAIL"
    exit 1
fi

# 2. Compare local HEAD to origin/main by full SHA (rebase-identical commits
#    commit-graph-equal but SHA-different; only full-SHA equality passes).
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main)
if [ "$LOCAL" != "$REMOTE" ]; then
    echo "GATE_FAIL: HEAD != origin/main" >&2
    echo "  local  = $LOCAL" >&2
    echo "  remote = $REMOTE" >&2
    echo "GATE_FAIL"
    exit 1
fi

# 3. Working tree must be clean (no uncommitted + no untracked).
if [ -n "$(git status -s)" ]; then
    echo "GATE_FAIL: working tree dirty (uncommitted or untracked changes)" >&2
    git status -sb >&2
    echo "GATE_FAIL"
    exit 1
fi

# All three checks passed.
echo "GATE_PASS"
exit 0
