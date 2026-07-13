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
#   ── Always-run developer gates ──
#   1. tools/check_main_clean.sh            (GATE-MNT-01 rebase-clean invariant)
#   2. tools/check_test_hygiene.sh          (gate #10b doctest no-duplicate-main)
#   3. tools/check_test_suite_registration.sh (gate #10c raw add_executable audit)
#   4. tools/check_frame_value_convention.sh (TICKET-110b gate)
#   4.5d. tools/check_no_changelog_conflict_markers.sh
#   4.5e. tools/check_text_golden_sources_aligned.sh
#   4.5f. tools/check_doc_sha_dedup.sh
#   4.5g. tools/check_commit_subject_length.sh (72-char envelope)
#   ── WBH-only gates (CHRONON3D_GATE_PROFILE=wbh) ──
#   4.5h. tools/check_video_completeness.sh (needs MP4 artifact)
#   4.5j. tools/check_manual_touches_per_video.sh (Test #19)
#   4.5k. tools/check_batch_100_videos.sh (Test #20)
#   4.5m. tools/check_glow_certification.sh
#   4.5n. tools/check_determinism.sh
#   4.5p. tools/check_determinism_matrix.sh
#   5. exec git push "$@" atomically
#
# Each gate exits 0 (pass) / 1 (fail) / 2 (internal-script-error).  Hardblock
# always; no --skip-gates escape hatch.  Documented in
# `docs/AGENT_WORKFLOW.md` §6 (Pre-push hygiene gates).
#
# Note (I1 audit remediation 2026-07-13): the M1.8 §1 "no parallel text
# API" invariant is structurally enforced by Gate #25 in
# `tools/check_architecture_boundaries.sh`. The wrapper no longer
# references any standalone script for this invariant; Gate #25 is the
# single canonical enforcement surface (4 categories:
# LayerBuilder::text_<variant>, centered_text/glow_text definitions,
# TextSpec.position assignments, pin_to+TextAnchor co-occurrence).
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

# ── Gate profile: developer (default) vs wbh (working build host) ──────────
# `developer` — fast local checks safe on any push (no MP4/build artifacts).
# `wbh`       — full video/glow/determinism/batch validation (needs build host).
GATE_PROFILE="${CHRONON3D_GATE_PROFILE:-developer}"
readonly GATE_PROFILE

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
fi
if [ "$AUTO_FF_SETTING" = "true" ] && [ -n "$REMOTE_COMMIT" ] \
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

# ── Run developer gates (delegated to canonical run_developer_gates.sh) ─
# The 8 developer gates live in tools/run_developer_gates.sh — single
# source of truth shared with .githooks/pre-push.  No duplication.
echo "wrap_push.sh: running developer gate chain (via run_developer_gates.sh ${TARGET_REMOTE} ${TARGET_BRANCH})..."
bash "${SCRIPT_DIR}/run_developer_gates.sh" "${TARGET_REMOTE}" "${TARGET_BRANCH}" \
    || { echo "wrap_push.sh: GATE_FAIL on run_developer_gates.sh (exit $?)" >&2; exit 1; }

# ── WBH-only gates (run only when CHRONON3D_GATE_PROFILE=wbh) ─────────────────
# These gates require build artifacts (MP4, glow output, batch videos) that
# only exist on a working build host.  On developer pushes they are skipped.
if [[ "$GATE_PROFILE" == "wbh" ]]; then

# Step 4.5h: Video completeness probe (TICKET-VIDEO-FFPROBE-VALIDATION)
echo "wrap_push.sh: checking video completeness probe (spec §4+§6 — ffprobe MP4 contract + 60-frame ffmpeg decode count)..."
bash "${SCRIPT_DIR}/check_video_completeness.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_video_completeness.sh (exit $?)" >&2; exit 1; }

echo "wrap_push.sh: checking fix-velocity cronograph (Test #11)..."
bash "${SCRIPT_DIR}/check_fix_cronograph.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_fix_cronograph.sh (exit $?)" >&2; exit 1; }

# Step 4.5j: Manual touches per video (Test #19)
echo "wrap_push.sh: checking manual_touches_per_video (Test #19) -- 4-phase thresholds (oggi<=8, fase1<=3, fase2<=1, finale<=0)..."
bash "${SCRIPT_DIR}/check_manual_touches_per_video.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_manual_touches_per_video.sh (exit $?)" >&2; exit 1; }

# Step 4.5k: Batch 100 videos acceptance (Test #20)
echo "wrap_push.sh: checking batch_100_videos (Test #20) -- 4 PASS-criteria envelopes (100 output / 0 crash / 0 corrotti / >=98% no manual)..."
bash "${SCRIPT_DIR}/check_batch_100_videos.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_batch_100_videos.sh (exit $?)" >&2; exit 1; }

# Step 4.5s: SDK consumer certification (TICKET-VERIFY-SDK-CONSUMER-FUNCTIONAL-LINUX)
bash "${SCRIPT_DIR}/verify_sdk_consumer_functional_linux.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on verify_sdk_consumer_functional_linux.sh (exit $?)" >&2; exit 1; }

# Step 4.5m: Glow certification (TICKET-GLOW-CERTIFICATION)
bash "${SCRIPT_DIR}/check_glow_certification.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_glow_certification.sh (exit $?)" >&2; exit 1; }

# Step 4.5n: Determinism gate (TICKET-DETERMINISM)
bash "${SCRIPT_DIR}/check_determinism.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_determinism.sh (exit $?)" >&2; exit 1; }

# Step 4.5p: Determinism matrix gate (TICKET-DETERMINISM-MATRIX)
bash "${SCRIPT_DIR}/check_determinism_matrix.sh" \
    || { echo "wrap_push.sh: GATE_FAIL on check_determinism_matrix.sh (exit $?)" >&2; exit 1; }

else
    echo "wrap_push.sh: GATE_PROFILE=${GATE_PROFILE} — skipping WBH-only gates (video/glow/determinism/batch/SDK)"
fi

# ── Step 4.5q: PERF_GATE pre-flight (F1.6 / TICKET-PERF-GATE-V1) ──────────────────
# Optional perf-regression gate executed when the env var `PERF_GATE=enabled`
# is set.  Default OFF (backward compat with the existing wrap_push.sh invocation
# surface).  When enabled:
#   - current  bench.v3 JSON path: ${CHRONON3D_PERF_CURRENT_REPORT:-""}
#   - baseline bench.v3 JSON path: ${CHRONON3D_PERF_BASELINE:-$REPO_ROOT/bench/baselines/main-HEAD-perf.json}
# Both paths must resolve (the gate is GATE_BLOCKED if either is missing); both
# paths must be valid bench.v3 JSON (the gate validates via tools/lib_perf_regression.py
# parse_bench()).
# Exit codes (mirror the verify_*_linux.sh family 3-state envelope):
#   0 = GATE_PASS  — proceed to git push.
#   1 = GATE_FAIL  — block push; emit remediation hint (src-side fix / regenerate
#                    baseline / widen threshold via forward-point ADR).
#   2 = GATE_BLOCKED — the gate cannot proceed (env-block on this VPS per
#                    TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV).  Default closure:
#                    when PERF_GATE=enabled and binary is missing, abort with
#                    remediation hint (operator explicitly opted in; closing
#                    BLOCKED would defeat the gate's purpose).  Forward-point:
#                    macchina-verifica on WBH per TICKET-PERF-GATE-V1-WBH-MACHINE-VERIFY.
if [[ "${PERF_GATE:-disabled}" == "enabled" ]]; then
    CURRENT_REPORT="${CHRONON3D_PERF_CURRENT_REPORT:-build/manual-test/perf_latest.json}"
    BASELINE_REPORT="${CHRONON3D_PERF_BASELINE:-$REPO_ROOT/bench/baselines/main-HEAD-perf.json}"
    if [[ ! -s "$CURRENT_REPORT" ]]; then
        echo "wrap_push.sh: PERF_GATE=enabled but CHRONON3D_PERF_CURRENT_REPORT unresolved: $CURRENT_REPORT" >&2
        echo "  fix: run 'chronon3d_cli bench <scene> --json-file $CURRENT_REPORT' first" >&2
        echo "  fix: or set CHRONON3D_PERF_CURRENT_REPORT=/path/to/bench-v3.json explicitly" >&2
        echo "GATE_FAIL"
        exit 1
    fi
    if [[ ! -s "$BASELINE_REPORT" ]]; then
        echo "wrap_push.sh: PERF_GATE=enabled but CHRONON3D_PERF_BASELINE unresolved: $BASELINE_REPORT" >&2
        echo "  fix: set CHRONON3D_PERF_BASELINE=/path/to/bench-v3-baseline.json" >&2
        echo "  fix: or place a default baseline at $BASELINE_REPORT (forward-point forward-ops)" >&2
        echo "GATE_FAIL"
        exit 1
    fi
    echo "wrap_push.sh: PERF_GATE pre-flight (current=$CURRENT_REPORT baseline=$BASELINE_REPORT)..."
    bash "${SCRIPT_DIR}/check_perf_regression.sh" \
        --current  "$CURRENT_REPORT" \
        --baseline "$BASELINE_REPORT"
    gate_rc=$?
    case $gate_rc in
        0) echo "wrap_push.sh: PERF_GATE PASSED — proceeding to git push"; ;;
        1) echo "wrap_push.sh: GATE_FAIL on check_perf_regression.sh — push aborted" >&2
           echo "  fix: see tools/check_perf_regression.sh diagnostic for offending metric(s)" >&2
           echo "  fix-forward paths: (a) src-side fix; (b) regenerate baseline; (c) widen threshold (ADR)" >&2
           exit 1 ;;
        2) echo "wrap_push.sh: GATE_BLOCKED on check_perf_regression.sh — push aborted (env-block on this VPS)" >&2
           echo "  fix: macchina-verifica DEFERRED-WBH per TICKET-PERF-GATE-V1-WBH-MACHINE-VERIFY" >&2
           echo "  fix: when PERF_GATE=enabled and binary is missing locally, gate reports BLOCKED" >&2
           echo "  fix: either set PERF_GATE=disabled to skip OR build chronon3d_cli locally first" >&2
           exit 1 ;;
        *) echo "wrap_push.sh: check_perf_regression.sh exited with unrecognized code $gate_rc — aborting" >&2
           exit 2 ;;
    esac
fi

echo "wrap_push.sh: gate PASSED — invoking: git push $*"
exec git push "$@"
