#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# wrap_push.sh — GATE-MNT-01: portable pre-push wrapper for `git push`
# ═══════════════════════════════════════════════════════════════════════════
#
# Auto fast-forward-merges remote commits into local before pushing, then
# runs the canonical gate (`tools/check_main_clean.sh`), and only forwards
# `git push "$@"` if the gate passes.  Drop-in replacement for `git push`
# (forwards all args including --force / --no-verify / refspec forms).
#
# Behaviour (post TICKET-076 closure, 2026-06-30):
#   1. Parse remote (default: origin) and refspec (default: current branch).
#   2. `git fetch "$REMOTE"` — bring remote refs up to date.
#   3. If HEAD != $REMOTE/$REFSPEC AND `is-ancestor HEAD REMOTE_REF`
#      (i.e., remote is descendant of local AND the history is linear so
#      an FF-merge is possible) — `git merge --ff-only "$REMOTE/$REFSPEC"`
#      to advance the local branch pointer automatically.  If FF fails
#      (true divergence caught at FF-time), reject with diagnostic +
#      manual-resolution hint; do not proceed to the gate.
#   4. Run the canonical gate (`tools/check_main_clean.sh`): rejects
#      divergence + dirty tree (post-FF working-tree state).
#   5. Forward `git push "$@"` on success.
#
# TICKET-067 closure lineage:
#   - TICKET-048: wrapped `git push` with `tools/check_main_clean.sh`.
#   - TICKET-067 / TICKET-075: relaxed strict-SHA equality to merge-base
#     ancestor relation (the gate accepts the "remote is descendant of
#     local" direction).
#   - TICKET-076 (this commit): the wrapper AUTOMATICALLY performs the
#     fast-forward merge on the remote-ahead case so a manual
#     `git pull --rebase origin <branch>` step is no longer required
#     between the gate failure and the next push.  Cognition speed-up:
#     one fewer command per Agent3 atomic-commit iteration when origin
#     has advanced (CI commits, partner squash, etc.).
#
# Rationale for the wrapper vs `.git/hooks/pre-push`:
#   - `.git/hooks/` is typically git-ignored (no cross-clone persistence)
#   - this wrapper is repo-tracked in `tools/`, present in every clone/worktree
#   - one canonical entry point for Agent3 atomic-commit workflow
#
# Usage:
#   tools/wrap_push.sh origin main
#   tools/wrap_push.sh                  # uses defaults (origin, current branch)
#   tools/wrap_push.sh --force origin HEAD
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
GATE="${REPO_ROOT}/tools/check_main_clean.sh"

if [ ! -x "$GATE" ]; then
    echo "wrap_push.sh: gate script missing or not executable: $GATE" >&2
    echo "  fix: chmod +x tools/check_main_clean.sh" >&2
    exit 2
fi

# ── Step 1: parse args (default remote=origin, refspec=current branch) ─────
# Match `git push` arg structure: optional flags first, then [remote [refspec]].
# We only consume the first two positional args; everything else forwards
# to `git push` unchanged (including --force, --no-verify, --tags, refspec
# forms like refs/tags/foo, etc.).
TARGET_REMOTE="origin"
TARGET_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
POSITIONAL_INDEX=0
for arg in "$@"; do
    case "$arg" in
        --*) ;;  # skip flags
        *)
            case $POSITIONAL_INDEX in
                0) TARGET_REMOTE="$arg" ;;
                1) TARGET_BRANCH="$arg" ;;
            esac
            POSITIONAL_INDEX=$((POSITIONAL_INDEX + 1))
            ;;
    esac
done

REMOTE_REF="${TARGET_REMOTE}/${TARGET_BRANCH}"

# ── Step 2: fetch remote refs ─────────────────────────────────────────────
if ! git fetch "$TARGET_REMOTE" 2>/dev/null; then
    echo "wrap_push.sh: GATE_FAIL: git fetch $TARGET_REMOTE failed" >&2
    echo "  fix: verify network/auth/remote config, then retry" >&2
    echo "GATE_FAIL"
    exit 1
fi

# ── Step 3: auto fast-forward if remote is ahead AND FF-pure ──────────────
# When HEAD != $REMOTE_REF AND is-ancestor HEAD REMOTE_REF (i.e., remote
# has commits the local doesn't, AND the history is linear so a FF-merge
# is possible), advance the local branch pointer automatically.  This is
# the new convenience layer on top of TICKET-067: the gate now accepts
# the FF direction, and the wrapper proactively executes the merge so
# the user does not have to.  FF-failure-with-divergence is caught here
# (with a richer diagnostic than the bare gate) so the user knows the
# exact manual remediation step.
LOCAL_REF="$(git rev-parse HEAD)"
REMOTE_COMMIT="$(git rev-parse "$REMOTE_REF" 2>/dev/null || echo "")"

if [ -n "$REMOTE_COMMIT" ] \
   && [ "$LOCAL_REF" != "$REMOTE_COMMIT" ] \
   && git merge-base --is-ancestor "$LOCAL_REF" "$REMOTE_COMMIT"; then
    echo "wrap_push.sh: auto-FF: $REMOTE_REF is fast-forward of HEAD — merging"
    if ! git merge --ff-only "$REMOTE_REF"; then
        # FF-merge failed → caught true divergence at FF-time (before
        # the gate).  Emit GATE_FAIL with diagnostic + manual-resolution
        # hint.  Distinguishes from gate-level rejection so the user
        # knows the manual rebase is needed (not a "retry later" issue).
        echo "" >&2
        echo "GATE_FAIL: remote is ahead but fast-forward not possible" >&2
        echo "  local  = $LOCAL_REF" >&2
        echo "  remote = $REMOTE_COMMIT" >&2
        echo "  fix: divergence requires manual intervention" >&2
        echo "    git pull --rebase $TARGET_REMOTE $TARGET_BRANCH" >&2
        echo "    OR: $TARGET_REMOTE fetch + manual merge" >&2
        echo "GATE_FAIL"
        exit 1
    fi
    echo "wrap_push.sh: auto-FF: merged remote commits into local"
fi

# ── Step 4: run the canonical gate (post-FF working-tree state) ───────────
echo "wrap_push.sh: GATE-MNT-01 pre-flight"
if ! "$GATE"; then
    echo "wrap_push.sh: gate FAILED — push aborted" >&2
    echo "  fix: ensure working tree clean (post-FF), then retry" >&2
    echo "  note: pre-push auto-FF has been applied; if the gate still rejects" >&2
    echo "        afterwards, the divergence is non-FF-able and manual rebase" >&2
    echo "        is required (see gate diagnostics above)." >&2
    exit 1
fi

# ── Step 5: forward to git push ───────────────────────────────────────────
echo "wrap_push.sh: gate PASSED — invoking: git push $*"
exec git push "$@"
