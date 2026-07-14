#!/usr/bin/env bash
# ============================================================================
# check_post_push_consistency.sh — CI-side lost-commit detector
# (AGENTS.md §Post-push SHA-selfcheck invariant canonical implementation)
# ============================================================================
#
# Companion to the agent-side SHA-triple check documented at AGENTS.md
# §Post-push SHA-selfcheck invariant.  The agent-side rule itself is:
#
#     POSTPUSH_SHA == UPSTREAM_SHA == LOCAL_SHA (captured before push
#     invocation by the agent workflow)
#
# This gate implements the same logic as a CI-side lint-checkability point per
# AGENTS.md §Regole di lint documentale §Post-push SHA-selfcheck rule
# forward-point (verbatim):
#
#     "if the last `push` reflog entry was preceded by a commit `C` whose SHA
#     does NOT appear in the current `origin/main` reflog, emit `GATE_FAIL:
#     post-push SHA-mismatch detected — chore <SHA> lost between local and
#     upstream`."
#
# Behaviour:
#   1. Run `git fetch origin` to ensure reflog is consistent.
#   2. Scan `git reflog show HEAD` for the most recent `commit:` reflog
#      entry — this is the commit `C` the agent thinks it just pushed (the
#      chore candidate).  `git commit` advances HEAD, so HEAD@{0} right after
#      `git commit` is `C`; subsequent wrapper invocations don't modify HEAD.
#   3. Verify `C` is reachable from `origin/main` (i.e., is in the upstream
#      history via `git merge-base --is-ancestor C origin/main`).
#   4. If NOT reachable → GATE_FAIL with explicit lost-commit pattern
#      diagnostic + AGENTS.md `21ece2b3 unique-edit recovery variant` template.
#   5. If reflog is empty (fresh clone with no prior push) → vacuously
#      satisfied GATE_PASS.
#
# Exit codes:
#   0 = clean (chore `C` is ancestor of origin/main — push landed correctly)
#   1 = GATE_FAIL (lost-commit pattern detected)
#   2 = GATE_INTERNAL_ERROR (git fetch failed, reflog unreadable, etc.)
#
# Diagnostic style (per AGENTS.md §Regole di lint documentale Rule #2
# INFO-level diagnostic style): `[INFO] ${GATE_NAME}:` reserved for the
# canonical clean-state diagnostic on PASS, additional to the `GATE_PASS` line.
#
# Stand-alone usage:
#   bash tools/check_post_push_consistency.sh           # lint-check default
#   bash tools/check_post_push_consistency.sh --strict  # exit 1 on empty reflog
#
# Invoked by:
#   - Stand-alone / CI post-push verification (NOT in the pre-push developer
#     chain, because a commit not yet pushed cannot be an ancestor of
#     origin/main).
#   - Can be wired into a CI pipeline after the push stage to detect the
#     lost-commit pattern described in AGENTS.md §Post-push SHA-selfcheck.
# ============================================================================

set -euo pipefail

GATE_NAME="check_post_push_consistency"

# ── Resolve repository root ────────────────────────────────────────────────
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT" || { echo "GATE_INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

# ── Step 0: bring remote refs up to date ───────────────────────────────────
if ! git fetch origin 2>/dev/null; then
    echo "GATE_INTERNAL_ERROR: ${GATE_NAME}: git fetch origin failed" >&2
    exit 2
fi

# ── Step 1: scan reflog for the most recent `commit:` action on HEAD ──────
# Head's reflog records `commit: <subject>` lines for each `git commit`.
# The first `commit:` line in HEAD's reflog (most recent) is the chore
# candidate `C` — the commit the agent just made and intended to push.
HEAD_REFLOG="$(git reflog show HEAD --format='%H %gs' 2>/dev/null || true)"

if [ -z "$HEAD_REFLOG" ]; then
    # Empty reflog is benign on a fresh clone (rare; mainly useful for the
    # post-clone bootstrap path).  Vacuously pass + leave a discoverable
    # diagnostic so the operator can see why we didn't flag.
    echo "GATE_PASS: HEAD reflog empty on fresh clone (no last-push to verify yet)"
    echo "[INFO] ${GATE_NAME}: HEAD reflog is empty — vacuously satisfied (no prior push on record; no lost-commit pattern can be detected)"
    exit 0
fi

CHORE_CANDIDATE_SHA="$(echo "$HEAD_REFLOG" | grep ' commit:' | head -1 | awk '{print $1}' || true)"

if [ -z "$CHORE_CANDIDATE_SHA" ]; then
    # No `commit:` action found — the local checkout has no chore to verify.
    # This is the post-WBH-FIX / post-bootstrap state (everything came via
    # clone, no `git commit` has been executed locally yet).
    echo "GATE_PASS: no `commit:` action in HEAD reflog (locale is clone-only; no chore to verify)"
    echo "[INFO] ${GATE_NAME}: no chore candidate found — vacuously satisfied"
    exit 0
fi

# ── Step 2: verify chore candidate `C` is reachable from origin/main ──────
# If ancestor-of-origin/main → push landed correctly (chore is upstream).
# If NOT ancestor → lost-commit pattern detected (chore rebased out by
# concurrent agent / auto-FF rodeo).
ORIGIN_TIP_SHA="$(git rev-parse origin/main)"

# Upstream-resolve guard: per code-reviewer-minimax-m3 MINOR-FIX on the cat-5
# 3-doc chore lineage. refuse to emit a misleading "chore lost between local and
# upstream" GATE_FAIL diagnostic when the actual root cause is an upstream ref
# misconfiguration (misconfigured clone / remote renamed / upstream ref retracted).
if ! git rev-parse origin/main >/dev/null 2>&1 || [ -z "$ORIGIN_TIP_SHA" ]; then
    echo "GATE_INTERNAL_ERROR: ${GATE_NAME}: origin/main unresolved (misconfigured clone / remote renamed / upstream ref retracted)" >&2
    echo "  fix: verify remote: git remote -v && git fetch origin" >&2
    echo "  fix: verify branch tracking: git branch --set-upstream-to=origin/main main  (if branch is main)" >&2
    echo "GATE_FAIL"
    exit 2
fi
if git merge-base --is-ancestor "$CHORE_CANDIDATE_SHA" origin/main 2>/dev/null; then
    # Clean: chore `C` is on origin/main's lineage.
    LOCAL_AHEAD_COUNT="$(git rev-list --count origin/main..HEAD 2>/dev/null || echo 0)"
    echo "GATE_PASS: chore $CHORE_CANDIDATE_SHA is ancestor of origin/main (origin_tip=$ORIGIN_TIP_SHA); no lost-commit pattern detected"
    echo "[INFO] ${GATE_NAME}: 0 lost-commit events detected — chore $CHORE_CANDIDATE_SHA is on origin/main lineage (local_ahead_commits=${LOCAL_AHEAD_COUNT})"
    exit 0
else
    # GATE_FAIL: the chore candidate `C` is NOT in origin/main's history →
    # this is the lost-commit pattern that bit the b589fdba 3-attempt
    # recovery session and that the AGENTS.md §Post-push SHA-selfcheck rule
    # was synthesized to prevent.
    LOCAL_TIP_SHA="$(git rev-parse HEAD)"
    echo "GATE_FAIL: post-push SHA-mismatch detected — chore $CHORE_CANDIDATE_SHA lost between local and upstream" >&2
    echo "  chore_candidate_sha = $CHORE_CANDIDATE_SHA (not in origin/main's history)" >&2
    echo "  origin_tip_now       = $ORIGIN_TIP_SHA" >&2
    echo "  local_head_now       = $LOCAL_TIP_SHA" >&2
    echo "  diagnosis: a concurrent agent's auto-FF between your `git commit` and `bash tools/wrap_push.sh` rebased out your chore" >&2
    echo "  fix: explicit reset + re-apply template per AGENTS.md §Post-push SHA-selfcheck invariant '21ece2b3 unique-edit recovery variant':" >&2
    echo "    git reset --hard '@{u}'    # drop the semantic-identical-to-upstream block" >&2
    echo "    # cherry-pick the UNIQUE non-conflicting portion of the original chore" >&2
    echo "    git commit -m '<unique fix subject>' && bash tools/wrap_push.sh origin main" >&2
    echo "GATE_FAIL"
    exit 1
fi
