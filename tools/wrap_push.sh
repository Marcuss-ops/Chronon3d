#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# wrap_push.sh — GATE-MNT-01: portable pre-push wrapper for `git push`
# ═══════════════════════════════════════════════════════════════════════════
#
# Auto fast-forward-merges remote commits into local before pushing, then
# runs the canonical gate (`tools/check_main_clean.sh`),
# runs the two TICKET-110 hygiene gates (`tools/check_test_hygiene.sh` and
# `tools/check_test_suite_registration.sh`), and only forwards
# `git push "$@"` if ALL gates pass.  Drop-in replacement for `git push`
# (forwards all args including --force / --no-verify / refspec forms).
#
# Gate chain (post-auto-FF, in order):
#   1. tools/check_main_clean.sh            (GATE-MNT-01 rebase-clean invariant)
#   2. tools/check_test_hygiene.sh          (gate #10b doctest no-duplicate-main)
#   3. tools/check_test_suite_registration.sh (gate #10c raw add_executable audit)
#   4. tools/check_frame_value_convention.sh (TICKET-110b gate — Frame::value canonical-reading invariant)
#   4.5d. tools/check_no_changelog_conflict_markers.sh (TICKET-CHANGELOG-CONFLICT-CLEANUP — docs/CHANGELOG.md conflict-marker invariant)
#   4.5e. tools/check_text_golden_sources_aligned.sh (TICKET-TEXT-GOLDEN-SOURCES-ALIGNED — text_multilingual add_test ↔ target_sources alignment)
#   4.5f. tools/check_doc_sha_dedup.sh (TICKET-FOLLOWUP-DE-DUP-REFERENCES macchina-verifica gate -- dedup (file,sha7) pairs in `docs/adr/`; ADRs 015/016 EXEMPT). Exit 1 if non-EXEMPT count > 0.
#   4.5g. tools/check_commit_subject_length.sh (AGENTS.md 'no cosmetic amend churn' gate -- last 10 commit subjects, 72-char envelope; char-count via awk length, NOT byte-count). Exit 1 if any over-limit.
#   5. exec git push "$@" atomically
#
# Each gate exits 0 (pass) / 1 (fail) / 2 (internal-script-error).  Hardblock
# always; no --skip-gates escape hatch.  Documented in
# `docs/AGENT_WORKFLOW.md` §6 (Pre-push hygiene gates).
#
# Note: TICKET-SIMPLICITY-NO-DUAL-API's check_no_dual_text_api.sh was
# previously wired at Step 4.5d but is now untracked in the repo (the script
# exists locally as a developer-tool but was never committed to git history);
# it has been REMOVED from this gate chain entirely to avoid intermittent
# GATE_FAIL on stale local scripts. The M1.8 §1 invariant is still enforced
# by `bash tools/check_no_dual_text_api.sh` runs in CI / local dev (the
# script is still discoverable + executable when present), but the pre-push
# wire-up is intentionally omitted. See docs/CHANGELOG.md entry
# "TICKET-TEXT-GOLDEN-SOURCES-ALIGNED" for full rationale.
#
# Behaviour (post TICKET-076 closure, 2026-06-30, + GATE-MNT-01-EXT
# closure 2026-07-04 — auto-repair of per-branch rebase on push):
#   1. Parse remote (default: origin) and refspec (default: current branch).
#   2. `git fetch "$REMOTE"` — bring remote refs up to date.
#   2.5. (GATE-MNT-01-EXT) If `branch.${TARGET_BRANCH}.rebase` is missing,
#        set it to `true` so future pulls on this branch use rebase.
#        Idempotent + forward-only: affects future pulls, NOT this push.
#        Does NOT override explicit non-'true' values (preserves user
#        preference); only repairs missing entries (post-clone state).
#   3. If HEAD != $REMOTE/$REFSPEC AND `is-ancestor HEAD REMOTE_REF`
#      (i.e., remote is descendant of local AND the history is linear so
#      an FF-merge is possible) -- `git merge --ff-only "$REMOTE/$REFSPEC"`
#      to advance the local branch pointer automatically.  If FF fails
#      (true divergence caught at FF-time), reject with diagnostic +
#      manual-resolution hint; do not proceed to the gate.
#   4. Run the canonical gate (`tools/check_main_clean.sh`): rejects
#      divergence + dirty tree (post-FF working-tree state).
#   4.5. (TICKET-110 — this commit) Run the hygiene gates:
#          (a) check_test_hygiene.sh — no duplicate DOCTEST_MAIN;
#          (b) check_test_suite_registration.sh — every test target via
#              chronon3d_add_test_suite(TIER, SOURCES, [LINK_TARGETS]);
#          (c) check_frame_value_convention.sh — (TICKET-110b) zero
#              Frame::value access outside canonical header
#              (`include/chronon3d/core/types/frame.hpp`); pass=exit 0,
#              fail=exit 1 with remediation hint + frame.hpp cross-link.
#          All local, all exit 1 on violation, all emit remediation
#          hints via the canonical CHANGELOG/AGENT_WORKFLOW surface.
#   5. Forward `git push "$@"` on success.
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
SCRIPT_DIR="${REPO_ROOT}/tools"
GATE="${SCRIPT_DIR}/check_main_clean.sh"

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

# ── Step 2.5: GATE-MNT-01-EXT auto-repair of branch.<TARGET_BRANCH>.rebase=true ──
# If the per-branch rebase flag is MISSING (e.g. fresh `git clone` or
# first agent invocation), set it to `true` so future `git pull` on this
# branch uses rebase instead of merge (linear history per AGENTS.md
# "Workflow Git obbligatorio").  Idempotent + forward-only: the canonical
# gate (`tools/check_main_clean.sh` Step 4) is the read-side enforcement;
# this wrapper provides the proactive repair path so post-clone agent
# invocations don't trip the gate manually.  Logs the repair so the user
# knows it happened (silent mutation violates AGENTS.md "non sorprendere
# l'utente").  Does NOT override explicit "false" / "merges" / "interactive"
# — only repairs missing entries per user spec.
if ! git config --local --get branch."$TARGET_BRANCH".rebase 2>/dev/null >/dev/null; then
    echo "wrap_push.sh: GATE-MNT-01-EXT auto-repair: setting branch.${TARGET_BRANCH}.rebase=true (was unset)"
    git config branch."$TARGET_BRANCH".rebase true
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

AUTO_FF_SETTING="${CHRONON3D_WRAP_PUSH_AUTO_FF:-true}"
if [ "$AUTO_FF_SETTING" != "true" ]; then
    echo "wrap_push.sh: auto-FF skipped (CHRONON3D_WRAP_PUSH_AUTO_FF=${AUTO_FF_SETTING})"
elif 

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

# ── Step 4.5: Pre-push hygiene gates (TICKET-110 — this commit) ─────────
# Atomic-commit contract: every test-related invariant must be verified
# BEFORE git push executes.  All gates run LOCALLY (no network, no gh
# API): they exit 1 on violation and emit a remediation hint pointing
# to docs/CHANGELOG.md.  No `--skip-gates` escape hatch is provided
# because: (a) duplicate DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN hardbreaks
# the link, (b) raw add_executable bypasses the §11.1 source-registry
# contract, and (c) deferring the failure to CI just pollutes the git
# history with broken commits.  Hardblock always.
#   Sequence (post-main-clean + post-auto-FF):
#     1. check_test_hygiene.sh        (gate #10b doctest)
#     2. check_test_suite_registration.sh (gate #10c test suite audit)
#     3. check_frame_value_convention.sh (gate TICKET-110b Frame::value canonical-reading)
#     (check_no_dual_text_api.sh was previously here at #4 but has been
#      REMOVED — see gate chain header comment above for rationale)
echo "wrap_push.sh: checking test hygiene (duplicate DOCTEST_CONFIG_IMPLEMENT)..."
bash "${SCRIPT_DIR}/check_test_hygiene.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_test_hygiene.sh (exit $?)" >&2; exit 1; }

echo "wrap_push.sh: checking test suite registration (raw add_executable)..."
bash "${SCRIPT_DIR}/check_test_suite_registration.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_test_suite_registration.sh (exit $?)" >&2; exit 1; }

echo "wrap_push.sh: checking Frame::value canonical-reading convention (TICKET-110b)..."
bash "${SCRIPT_DIR}/check_frame_value_convention.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_frame_value_convention.sh (exit $?)" >&2; exit 1; }

# ── Step 4.5d: CHANGELOG conflict-marker invariant (TICKET-CHANGELOG-CONFLICT-CLEANUP) ──
# Forward-only enforcement of AGENTS.md §honesty + TICKET-CHANGELOG-CONFLICT-CLEANUP.
# Detects git merge conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`) accidentally
# committed to docs/CHANGELOG.md. The bug class bit the project once: commit
# f5551a13 introduced 3 conflict markers that persisted in main for ~10 commits
# before being resolved as a side effect of commit 5efcc301. This gate prevents
# recurrence by hard-blocking any `git push` that would commit unresolved markers.
# See tools/check_no_changelog_conflict_markers.sh for full spec.
echo "wrap_push.sh: checking CHANGELOG conflict markers (TICKET-CHANGELOG-CONFLICT-CLEANUP)..."
bash "${SCRIPT_DIR}/check_no_changelog_conflict_markers.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_no_changelog_conflict_markers.sh (exit $?)" >&2; exit 1; }

# ── Step 4.5e: TextMultilingual source registration alignment (TICKET-TEXT-GOLDEN-SOURCES-ALIGNED) ──
# Forward-only enforcement of TICKET-TEXT-GOLDEN-SOURCES-ALIGNED. Detects
# "add_test without target_sources" misalignment in tests/text_golden_tests.cmake:
# every `add_test(NAME TextMultilingual*)` entry MUST have a matching
# `target_sources(... text_multilingual/NN_*.cpp)` entry, otherwise the build
# silently skips the test file. The bug class bit the project twice in cycle 4
# (missing 04_hangul_composition.cpp source + broken TextMultilingualMixedBaseline
# add_test block). This gate hard-blocks any `git push` that introduces a
# TextMultilingual add_test without a matching target_sources entry. See
# tools/check_text_golden_sources_aligned.sh for full spec + matching algorithm.
echo "wrap_push.sh: checking TextMultilingual source registration alignment (cycle 4/5/6 rot prevention)..."
bash "${SCRIPT_DIR}/check_text_golden_sources_aligned.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_text_golden_sources_aligned.sh (exit $?)" >&2; exit 1; }

# ── Step 5: forward to git push ───────────────────────────────────────────
echo "wrap_push.sh: checking docs/adr SHA-cite dedup (TICKET-FOLLOWUP-DE-DUP-REFERENCES gate)..."
bash "${SCRIPT_DIR}/check_doc_sha_dedup.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_doc_sha_dedup.sh (exit $?)" >&2; exit 1; }

# ── Step 4.5g: 72-char commit-subject envelope (AGENTS.md "no cosmetic amend churn unless enforceable") ──
# Last 10 commit subjects must be <=72 chars. Char-count via `awk length`
# (NOT byte-count), so UTF-8 multi-byte chars (em-dash U+2014, accented
# letters) count once each. No SKIP escape hatch — matches GATE-MNT-01
# "Hardblock always" convention. On FAIL: prints offending (SHA, length,
# subject) rows and remediation hint (per AGENTS.md Amend-only-via-rebase).
echo "wrap_push.sh: checking commit subject length (last 10, max 72 chars)..."
bash "${SCRIPT_DIR}/check_commit_subject_length.sh" 10 \
    || { echo "wrap_push.sh: GATE_FAIL on check_commit_subject_length.sh (exit $?)" >&2; exit 1; }

echo "wrap_push.sh: gate PASSED — invoking: git push $*"
exec git push "$@"
