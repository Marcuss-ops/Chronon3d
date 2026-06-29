#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# wrap_push.sh — GATE-MNT-01: portable pre-push wrapper for `git push`
# ═══════════════════════════════════════════════════════════════════════════
#
# Calls `tools/check_main_clean.sh` first; only invokes `git push` if the
# gate passes.  Drop-in replacement for `git push` (forwards all args).
#
# Rationale for choosing the portable wrapper over `.git/hooks/pre-push`:
#   - `.git/hooks/` is typically git-ignored (no cross-clone persistence)
#   - this wrapper is repo-tracked in `tools/`, present in every clone/worktree
#   - one canonical entry point for Agent3 atomic-commit workflow
#   - the local `.git/hooks/pre-push` is kept as defence-in-depth (auto-
#     installed during the agent's first commit in a fresh clone)
#
# Usage:
#   tools/wrap_push.sh origin main
#   tools/wrap_push.sh                  # uses defaults (origin, current branch)
#   tools/wrap_push.sh --force origin HEAD
#
# Idempotent + rebase-friendly: the gate itself is idempotent so after a
# `git pull --rebase origin main && tools/wrap_push.sh origin main` cycle
# the gate will simply pass once conditions are met.
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
GATE="${REPO_ROOT}/tools/check_main_clean.sh"

if [ ! -x "$GATE" ]; then
    echo "wrap_push.sh: gate script missing or not executable: $GATE" >&2
    echo "  fix: chmod +x tools/check_main_clean.sh" >&2
    exit 2
fi

echo "wrap_push.sh: GATE-MNT-01 pre-flight"
if ! "$GATE"; then
    echo "wrap_push.sh: gate FAILED — push aborted" >&2
    echo "  fix: git pull --rebase origin main (or fetch + sync), ensure" >&2
    echo "       working tree clean, then retry." >&2
    exit 1
fi

echo "wrap_push.sh: gate PASSED — invoking: git push $*"
exec git push "$@"
