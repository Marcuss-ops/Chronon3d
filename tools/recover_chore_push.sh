#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# recover_chore_push.sh — race-recovery push wrapper (P0 forward-point
# TICKET-RECOVERY-PATTERN-EXTRACT, closure 2026-07-12)
# ═══════════════════════════════════════════════════════════════════════════
#
# Encodes the WRITE-side belt-and-suspenders for AGENTS.md §Post-push
# SHA-selfcheck invariant: after every `bash tools/wrap_push.sh origin main`
# invocation that exits 0, the agent MUST verify a **SHA-triple equality**:
# `git rev-parse HEAD` (post-push) equals `git rev-parse '@{u}'` (upstream
# tracking) equals the local SHA captured BEFORE the push invocation.
#
# This invariant closes the lost-commit failure mode that bit the project
# in the `b589fdba` 3-attempt recovery session (TICKET-SOURCE-CONFLICT-
# MARKERS-ROT §honesty closure) and the +4 race iterations in this session
# (Steps 4, 6, 11, 12).  Pre-this-script, every agent reinvented the
# 30-line retry loop in a basher call; the pattern is now stable and
# extracted here as a single canonical entry point.
#
# Closure lineage (5 canonical sources, WRITE-side complement of the
# READ-side GATE-MNT-01 triad in AGENTS.md §GATE-MNT-01):
#   1. TICKET-048 (gate wrap)                 : the GATE_FAIL diagnostic convention
#   2. TICKET-067/075 (merge-base ancestor)   : the FF-pull + post-commit-push semantic
#   3. TICKET-076 (auto-FF in wrapper)        : the in-wrapper `git merge --ff-only`
#   4. GATE-MNT-01-EXT (per-branch rebase)     : the `branch.${TARGET}.rebase=true`
#                                                auto-repair (Step 2.5 of wrap_push.sh)
#   5. AGENTS.md §Post-push SHA-selfcheck     : the SHA-triple equality requirement
#      (codified in commit 4cfceca9, post `b589fdba` 3-attempt recovery)
#      that this script enforces.
#
# Distinguishes RACE failure from GATE failure:
#   - RACE     : wrap_push.sh exits 0 but SHA-triple is NOT satisfied
#                (lost-commit pattern: concurrent-agent pushed between
#                auto-FF and final push).  RECOVERABLE — retry with sleep.
#   - GATE     : wrap_push.sh exits non-zero (some check_*.sh hard-blocked).
#                NOT RECOVERABLE — surface error, exit 1, do NOT retry.
#
# Exit codes (canonical per AGENTS.md §honesty convention):
#   0 = SHA-TRIPLE EQUALITY verified on origin/main (chore landed cleanly,
#       pre-push local SHA == post-push local SHA)
#   1 = GATE_FAIL — one of three lost-commit / gate-failure patterns:
#       (a) wrap_push.sh rejected the push for a non-race reason
#       (b) SHA-DRIFT: pre-push local SHA != post-push local SHA but
#           SHA-triple is satisfied (chore was rebased out by upstream
#           churn; origin/main HEAD is some other commit, NOT the chore).
#           This is the TICKET-SOURCE-CONFLICT-MARKERS-ROT §honesty
#           closure pattern from the b589fdba 3-attempt recovery.
#       (c) SHA MISMATCH after max_iter iterations (race-recovery gave up;
#           chore is still locally-ahead of origin/main)
#   2 = INTERNAL_ERROR — sanity-check failures: not in a git repo,
#       wrap_push.sh not executable, max_iter < 1
#
# Usage:
#   tools/recover_chore_push.sh origin main                # default (20 iter)
#   tools/recover_chore_push.sh origin main 5              # 5 iterations
#   CHRONON3D_RECOVER_MAX_ITER=50 tools/recover_chore_push.sh origin main
#
# The script is dogfooded by being used in the TICKET-RECOVERY-PATTERN-EXTRACT
# closure commit itself: this chore is pushed via this script.  If the
# dogfooding fails, the closure commit rolls back to a manual basher loop
# (the previous pattern).  See TICKET-RECOVERY-PATTERN-EXTRACT forward-point
# documentation for the full failure-mode analysis.
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || { echo "recover_chore_push.sh: not in a git repo" >&2; exit 2; })"
cd "$REPO_ROOT" || { echo "recover_chore_push.sh: cannot cd to $REPO_ROOT" >&2; exit 2; }

# Parse args (forward to wrap_push.sh unchanged).  Optional 3rd arg = max
# iterations override; default + env override honored.
MAX_ITER="${CHRONON3D_RECOVER_MAX_ITER:-20}"
ARGS=()
for arg in "$@"; do
    case "$arg" in
        # If the arg is a positive integer, treat as max_iter override.
        ''|*[!0-9]*) ARGS+=("$arg") ;;
        *) MAX_ITER="$arg" ;;
    esac
done

if [ "$MAX_ITER" -lt 1 ]; then
    echo "recover_chore_push.sh: invalid max_iter=$MAX_ITER (must be >= 1)" >&2
    exit 2
fi

# Sanity: tools/wrap_push.sh must be executable
WRAP="${REPO_ROOT}/tools/wrap_push.sh"
if [ ! -x "$WRAP" ]; then
    echo "recover_chore_push.sh: $WRAP not executable (fix: chmod +x tools/wrap_push.sh)" >&2
    exit 2
fi

# Capture pre-push local SHA (per AGENTS.md §Post-push SHA-selfcheck invariant).
LOCAL_SHA_PRE="$(git rev-parse HEAD)"
echo "recover_chore_push.sh: pre-push local SHA = $LOCAL_SHA_PRE"
echo "recover_chore_push.sh: max_iter = $MAX_ITER"
echo "recover_chore_push.sh: pushing via $WRAP ${ARGS[*]:-(default origin + current branch)}"

# Race-recovery loop: invoke wrap_push.sh, then verify SHA-triple.
# Distinguish race (wrap=0 but SHA-triple !=) from gate (wrap != 0).
# On race: sleep + abort any paused rebase + retry.
# On gate: do NOT retry — surface the error and exit 1.
GATE_FAIL=0
for i in $(seq 1 "$MAX_ITER"); do
    LOCAL="$(git rev-parse HEAD)"
    ORIGIN="$(git rev-parse origin/main 2>/dev/null || echo '')"
    UPSTREAM="$(git rev-parse '@{u}' 2>/dev/null || echo '')"

    echo "--- iter $i / $MAX_ITER : local=$(git rev-parse --short "$LOCAL" 2>/dev/null) origin=$(git rev-parse --short "$ORIGIN" 2>/dev/null) upstream=$(git rev-parse --short "$UPSTREAM" 2>/dev/null) ---"

    # Pre-condition: if a paused rebase is in progress from a prior
    # iteration, abort it before retrying (the auto-FF inside wrap_push.sh
    # will re-establish the desired state).
    if [ -d .git/rebase-merge ] || [ -d .git/rebase-apply ]; then
        echo "  -- aborting paused rebase from prior iteration --"
        git rebase --abort 2>&1 | tail -n 1
    fi

    # Pre-condition: if already at SHA-triple equality, nothing to do.
    if [ -n "$ORIGIN" ] && [ "$LOCAL" = "$ORIGIN" ] && [ "$ORIGIN" = "$UPSTREAM" ]; then
        echo "  ✓✓✓ ALREADY AT SHA-TRIPLE — nothing to push"
        echo "recover_chore_push.sh: SUCCESS (iter $i, no push needed)"
        exit 0
    fi

    # Invoke the canonical wrapper.
    "$WRAP" "${ARGS[@]}"
    WRAP_RC=$?

    # Re-resolve SHAs after the push attempt.
    LOCAL="$(git rev-parse HEAD)"
    ORIGIN="$(git rev-parse origin/main 2>/dev/null || echo '')"
    UPSTREAM="$(git rev-parse '@{u}' 2>/dev/null || echo '')"

    # GATE failure path: wrap_push.sh rejected the push (non-zero exit).
    # This is NOT a race — it's a real gate failure (e.g., a check_*.sh
    # hard-blocked).  Surface the error and exit 1; do NOT retry, because
    # retrying without fixing the gate will just re-fail the same way.
    if [ "$WRAP_RC" != '0' ]; then
        echo "" >&2
        echo "recover_chore_push.sh: GATE_FAIL (wrap_push.sh exited $WRAP_RC at iter $i)" >&2
        echo "  This is a real gate failure, NOT a race." >&2
        echo "  Fix the gate failure and re-run; do NOT retry blindly." >&2
        echo "  See the wrap_push.sh output above for the offending gate." >&2
        exit 1
    fi

    # RACE failure path: wrap_push.sh exited 0 but SHA-triple is NOT
    # satisfied.  This is the lost-commit pattern (concurrent-agent
    # pushed between auto-FF and final push).  Retry with sleep.
    if [ -n "$ORIGIN" ] && [ "$LOCAL" = "$ORIGIN" ] && [ "$ORIGIN" = "$UPSTREAM" ]; then
        echo "  ✓✓✓ SHA-TRIPLE EQUALITY at iter $i: $LOCAL"
        # Verify the chore SHA is the one we intended to push.  If the
        # post-push local SHA != pre-push local SHA, the chore commit
        # was rebased out by upstream churn (the `b589fdba` 3-attempt
        # recovery pattern).  Per AGENTS.md §honesty, the script must
        # NOT exit 0 in this case — the agent's "pushed" report is
        # false (origin/main HEAD is now some upstream commit, NOT the
        # chore).  Surface the lost-commit with full diagnostics and
        # exit 1 (GATE_FAIL lost-commit; matches the §honesty convention
        # of exit 1 for "the chore is broken/lost").
        if [ "$LOCAL" != "$LOCAL_SHA_PRE" ]; then
            echo "" >&2
            echo "recover_chore_push.sh: SHA-DRIFT — CHORE LOST" >&2
            echo "  pre-push  local SHA = $LOCAL_SHA_PRE  (the chore commit)" >&2
            echo "  post-push local SHA = $LOCAL  (upstream HEAD, NOT the chore)" >&2
            echo "  origin/main  SHA   = $ORIGIN" >&2
            echo "" >&2
            echo "The chore commit ($LOCAL_SHA_PRE) was rebased out by upstream" >&2
            echo "churn between the auto-FF and the final push.  SHA-triple is" >&2
            echo "satisfied (HEAD == origin/main == @{u}) but origin/main HEAD is" >&2
            echo "NOT the chore — it is whatever concurrent-agent committed." >&2
            echo "Per AGENTS.md §Post-push SHA-selfcheck invariant, this is a" >&2
            echo "lost-commit pattern; the script MUST NOT exit 0." >&2
            echo "" >&2
            echo "Manual recovery (TICKET-SOURCE-CONFLICT-MARKERS-ROT §honesty" >&2
            echo "closure pattern from the b589fdba 3-attempt recovery session):" >&2
            echo "  1. Inspect git log origin/main for your chore's content" >&2
            echo "     (files + commit message subject line)" >&2
            echo "  2. If chore content is present (e.g. via rebase-merge):" >&2
            echo "     the chore landed, no action needed (the SHA is just" >&2
            echo "     different from the pre-push SHA)" >&2
            echo "  3. If chore content is ABSENT: re-create the chore and re-push" >&2
            echo "     git fetch origin && git rebase origin/main" >&2
            echo "     <re-apply chore files> <git add> <git commit --amend>" >&2
            echo "     tools/recover_chore_push.sh origin main" >&2
            echo "GATE_FAIL: chore commit $LOCAL_SHA_PRE not on origin/main HEAD ($LOCAL)"
            exit 1
        fi
        echo "recover_chore_push.sh: SUCCESS (iter $i)"
        exit 0
    fi

    echo "  ✗ iter $i race-recovery: wrap=0 but SHA-triple != (local=$LOCAL origin=$ORIGIN upstream=$UPSTREAM)"
    # Sleep to let concurrent agents complete their push before retrying.
    # Adaptive: first 5 iter use 2s, then 5s for the rest (give concurrent
    # agents enough time to land their chore).
    if [ "$i" -lt 5 ]; then
        sleep 2
    else
        sleep 5
    fi
done

# Exhausted max_iter iterations without achieving SHA-triple.  Surface the
# lost-commit pattern with full diagnostics for operator triage.  Exit 1
# (GATE_FAIL lost-commit) per the §honesty convention: the chore is broken
# / lost, not an internal-script error (which would be exit 2).
echo "" >&2
echo "recover_chore_push.sh: SHA MISMATCH after $MAX_ITER iterations" >&2
echo "  pre-push local SHA = $LOCAL_SHA_PRE" >&2
echo "  current local SHA  = $(git rev-parse HEAD)" >&2
echo "  current origin SHA = $(git rev-parse origin/main)" >&2
echo "  current upstream   = $(git rev-parse '@{u}')" >&2
echo "" >&2
echo "This is the lost-commit pattern: the chore did NOT land on origin/main" >&2
echo "after $MAX_ITER attempts.  Likely cause: heavy concurrent-agent pressure" >&2
echo "on origin/main (multiple agents pushing in rapid succession)." >&2
echo "Manual recovery per AGENTS.md §Post-push SHA-selfcheck invariant:" >&2
echo "  1. Inspect git log origin/main for your chore's content (files + commit msg)" >&2
echo "  2. If present: chore landed via rebase-merge, no action needed (exit 1 is conservative)" >&2
echo "  3. If absent: rebase local chore onto origin/main and re-push manually" >&2
echo "     git fetch origin && git rebase origin/main && git push origin main" >&2
echo "     (with the union-merge script for CHANGELOG + FOLLOWUP_TICKETS)" >&2
echo "GATE_FAIL: lost-commit pattern not resolved after $MAX_ITER attempts"
exit 1
