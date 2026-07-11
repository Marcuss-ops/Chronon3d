#!/usr/bin/env bash
set -euo pipefail

# ── check_test_suite_registration.sh ────────────────────────────────────────
#
# Audit gate: lists all test cmake files that use raw add_executable
# instead of chronon3d_add_test_suite().
#
# PROMOTED TO BLOCCANTE 2026-07-08 (this commit) — the §11.1 / §12.1
# migration backlog is now CLOSED (`git log` since this commit shows
# 18 raw add_executable calls → 0; 21 chronon3d_add_test_suite → 41
# covering all 41 test targets).  New raw add_executable calls in
# tests/*.cmake are now a GATE FAIL instead of an informational note.
#
# The original migration chain (scene_tests.cmake, visual_tests.cmake,
# core_tests.cmake, sdk_tests.cmake → suite; then this commit) is
# documented in docs/CHANGELOG.md §"Luglio 2026 — test-suite
# migration close-out".
#
# Exit codes:
#   0 = GATE_PASS  — all test targets use chronon3d_add_test_suite()
#   1 = GATE_FAIL  — at least one raw add_executable(chronon3d_*test ...) remains
#   2 = internal script error (rg/grep unavailable, REPO_ROOT missing, …)
# ────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ ! -d "$REPO_ROOT/tests" ]; then
    echo "GATE_FAIL (internal): $REPO_ROOT/tests is not a directory"
    exit 2
fi

echo "=== Test Suite Registration Audit ==="
echo ""

raw_count=0
suite_count=0
raw_files=()

for f in "$REPO_ROOT"/tests/*.cmake; do
    [ -f "$f" ] || continue
    basename=$(basename "$f")

    # Count add_executable calls that define a chronon3d_*test* target.
    # Excludes ${TEST_MAIN} references and comment-only lines. Filtering
    # on the leading `^\s*` ensures we don't match `target_sources(... PRIVATE
    # add_executable ...)` (which is a string literal in a comment).
    #
    # BAND-AID (forward-point 0k+): `| tr -d '\n'` patches a set -euo
    # pipefail bug where `grep` exiting 1 (no match) causes `wc -l` to
    # output '0\n' and the `|| echo 0` fallback to append another '0\n',
    # producing literal '0\n0' (multi-line) which `[[ ... -gt 0 ]]` cannot
    # parse as a single integer → 38 'syntax error in expression' stderr
    # lines per gate run. The `tr` strips the embedded newline, yielding
    # '00', which bash cleanly parses as octal 0 (and evaluates to 0).
    # CANONICAL FIX (NOT applied here per user-spec): drop `|| echo 0`
    # entirely and let `wc -l` be the final pipeline step.
    # ACCEPTED TRADE-OFF: if `grep` crashes mid-read with an I/O error
    # (extremely unlikely on local repo files), the `|| echo 0` fallback
    # would falsely append a `0` to a partial `wc -l` count, giving N0
    # instead of N. The thinker-with-files-gemini design verdict (0k+
    # closure) marks this as an acceptable band-aid trade-off.
    raw=$(grep -E '^\s*add_executable\(\s*chronon3d_.*_tests?\b' "$f" 2>/dev/null | wc -l | tr -d '\n' || echo 0)
    # Count chronon3d_add_test_suite() invocations (not comments). Same
    # band-aid rationale as above.
    suite=$(grep -E '^\s*chronon3d_add_test_suite\(' "$f" 2>/dev/null | wc -l | tr -d '\n' || echo 0)

    if [[ ${raw:-0} -gt 0 ]]; then
        echo "  RAW  $basename  ($raw targets)"
        raw_files+=("$basename")
        raw_count=$((raw_count + raw))
    fi
    if [[ ${suite:-0} -gt 0 ]]; then
        echo "  SUITE $basename ($suite targets)"
        suite_count=$((suite_count + suite))
    fi
done

echo ""
total=$((raw_count + suite_count))
echo "Summary: $suite_count SUITE / $raw_count RAW (total: $total targets)"

if [ "$raw_count" -gt 0 ]; then
    echo ""
    echo "GATE_FAIL: $raw_count raw add_executable call(s) remain in:"
    if [[ ${#raw_files[@]} -gt 0 ]]; then
        for f in "${raw_files[@]}"; do
            echo "  - tests/$f"
        done
    fi
    echo ""
    echo "All test targets MUST use chronon3d_add_test_suite(NAME <name>"
    echo "TIER <UNIT|INTEGRATION> SOURCES <list> [LINK_TARGETS <list>]"
    echo "[LABELS <list>]) defined in cmake/Chronon3DTestSuite.cmake (§11.1)."
    echo ""
    echo "See docs/CHANGELOG.md §\"Luglio 2026 — test-suite migration close-out\""
    echo "for the canonical migration pattern + per-file examples."
    exit 1
fi

echo ""
echo "GATE_PASS: 0 raw add_executable — all $total test targets use chronon3d_add_test_suite()"
echo "[INFO] check_test_suite_registration: 0 raw matches found — clean state (all $suite_count suite invocations verified across $(printf '%s\n' "$REPO_ROOT"/tests/*.cmake | wc -l) test cmake files)"
exit 0
