#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_repo_artifacts.sh — Anti-generated-files GATE for chrono3d repo
# ═══════════════════════════════════════════════════════════════════════════
#
# Blocks accidental re-introduction of build artifacts into the git index:
#   - CMake/Ninja generated files (build.ninja, CMakeCache.txt, …)
#   - CTest/CMake auxiliary files (cmake_install.cmake, CTestTestfile.cmake, …)
#   - install_manifest outputs
#   - .tmp_gate* scratch directories used by install-consumer tests
#
# This gate complements .gitignore: the gitignore prevents *new* artifacts from
# being staged; this gate catches the case where someone runs `git add -A` /
# `git add .tmp_gate10` on a working tree that already has artifacts, OR a
# workflow commits the directory from outside the .gitignore envelope.  It is
# the canonical machine-check for the "no generated files in VCS" invariant
# declared in AGENTS.md and in docs/CURRENT_STATUS.md.
#
# Behaviour:
#   - On success: stdout "GATE_PASS", exit 0
#   - On failure: stdout "GATE_FAIL", exit 1, diagnostic listing offending
#     files on stderr
#   - Idempotent: safe to invoke from pre-push hooks, CI runners, and agent
#     atomic-commit workflows.  Does NOT modify the working tree or index.
#
# Invocation:
#   tools/check_repo_artifacts.sh                          # exit-code check
#   bash tools/check_repo_artifacts.sh && echo PASS || echo FAIL
#
# Future-proofing: patterns below MUST stay in sync with `.gitignore`.  A
# follow-up can wire a self-test that diff-checks the two lists to detect
# drift; this script intentionally hard-codes them for now (one source of
# truth: docs/CURRENT_STATUS.md "Repository hygiene" + this file's header).
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

# ── Pattern set ──────────────────────────────────────────────────────────
# Two regexes (extended regex) are applied to `git ls-files` output.
#
# 1. ARTIFACT_REGEX — matches any of the known generated files at the
#    repository root OR nested in subdirectories.  Anchoring (^/|/$) ensures
#    we hit the basename, not just any substring inside a path.
#
# 2. TMP_REGEX — matches any tracked path that lives directly under
#    `.tmp_gate*` scratch directories (e.g. .tmp_gate10/…, .tmp_gate9/…).
#    This catches the failure mode where someone bypasses .gitignore by
#    `git add .tmp_gate10` before the .gitignore was added.
ARTIFACT_REGEX='(^|/)(CMakeCache\.txt|build\.ninja|compile_commands\.json|cmake_install\.cmake|CTestTestfile\.cmake|\.ninja_(log|deps)|install_manifest\.txt|install_manifest\.json)$'
# Matches .tmp_gate10/, .tmp_foo/, .tmp_anything/ — mirrors the .gitignore
# patterns `.tmp_gate*/` and `.tmp_*/`.  Anchor at start of path (these dirs
# live at the repo root by convention).
TMP_REGEX='^\.tmp_'

TRACKED_FILES="$(git ls-files)"

# Empty ls-files output is still valid (no tracked files) — guard with
# `[ -n "$TRACKED_FILES" ]` only when we need to grep.
FAIL=0

# ── Check 1: CMake / Ninja generated files ──────────────────────────────
if [ -n "$TRACKED_FILES" ] \
   && echo "$TRACKED_FILES" | grep -E "$ARTIFACT_REGEX" >/dev/null 2>&1; then
    echo "GATE_FAIL: tracked CMake/Ninja build artifact(s) detected" >&2
    echo "Offending paths:" >&2
    echo "$TRACKED_FILES" \
        | grep -E "$ARTIFACT_REGEX" \
        | sort -u \
        | sed 's/^/  /' >&2
    FAIL=1
fi

# ── Check 2: .tmp_gate* scratch directories ─────────────────────────────
if [ -n "$TRACKED_FILES" ] \
   && echo "$TRACKED_FILES" | grep -E "$TMP_REGEX" >/dev/null 2>&1; then
    echo "GATE_FAIL: tracked file(s) under .tmp_gate* scratch directory" >&2
    echo "Offending paths (head -20):" >&2
    echo "$TRACKED_FILES" \
        | grep -E "$TMP_REGEX" \
        | sort -u \
        | head -20 \
        | sed 's/^/  /' >&2
    HIT_COUNT=$(echo "$TRACKED_FILES" | grep -E "$TMP_REGEX" | wc -l)
    echo "  ($HIT_COUNT total hits)" >&2
    FAIL=1
fi

# ── Verdict ──────────────────────────────────────────────────────────────
if [ "$FAIL" -ne 0 ]; then
    echo "GATE_FAIL"
    exit 1
fi

echo "GATE_PASS"
exit 0
