#!/usr/bin/env bash
# ==============================================================================
# tools/check_no_source_conflict_markers.sh
# ==============================================================================
#
# Cat-5 rot-preventive gate. Closes the TICKET-SOURCE-CONFLICT-MARKERS-ROT class
# of bugs (10 files committed to main with unresolved `<<<<<<< HEAD` /
# `=======` / `>>>>>>> ...` markers as of 2026-07-12) by hard-blocking any
# future push that re-introduces them in SOURCE code.
#
# SCOPE (deliberately narrower than docs/CHANGELOG.md scope):
#   .cpp, .hpp, .h, .c, .cmake        — production source + CMake config.
#   EXCLUDES: .py (intentional selftest markers), .md (prose / docstring
#   mentions of marker syntax).
#
# Cat-5 / AGENTS.md INFO-level diagnostic style (Lint rule #2): emits a
# one-line [INFO] diagnostic on PASS in addition to the canonical GATE_PASS.
# (See AGENTS.md "Regole di lint documentale" §INFO-level diagnostic style.)
#
# Exit codes (custom):
#   0 = clean (no source-code conflict markers)
#   1 = at least one source file has unresolved conflict markers (FAIL)
#   2 = internal error (e.g. not in a git repo)
#
# Wire-in: should be added to tools/wrap_push.sh Step 4.5 in a follow-up
# commit (out of scope for the foundation commit per AGENTS.md "Fare PR
# piccole e mirate").
# ==============================================================================

set -euo pipefail

GATE_NAME=check_no_source_conflict_markers
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

# Default scan paths: SOURCE code only (deliberately excludes .py / .md).
# Override via SOURCE_CONFLICT_SCAN_PATHS env var.
SCAN_PATHS="${SOURCE_CONFLICT_SCAN_PATHS:-*.cpp *.hpp *.h *.c *.cmake}"

# Line-start anchored regex: a conflict marker ALWAYS starts a line in real
# conflict blocks. Avoids false positives from comments/strings like
# `<<<<<<< vs HEAD` or `======= (note: 7 chars, the separator)`.
HITS_FILE="/tmp/${GATE_NAME}_hits.txt"

# Use git grep (fast, indexed) with extended regex so we can anchor line start.
# Exits 0 on matches found, 1 on no matches; we neutralise via || true.
git grep -nE '^(<<<<<<< HEAD|=======$|>>>>>>> )' -- $SCAN_PATHS > "$HITS_FILE" 2>&1 || true

if [ -s "$HITS_FILE" ]; then
    hit_count="$(wc -l < "$HITS_FILE" | tr -d ' ')"
    echo "GATE_FAIL: $hit_count unresolved git merge conflict markers in source code:" >&2
    sed 's/^/    /' "$HITS_FILE" >&2
    echo "" >&2
    echo "REMEDIATION:" >&2
    echo "  1. Resolve each conflict block manually (run bash tools/resolve_rebase_conflict.py for the canonical guide)." >&2
    echo "  2. OR: if these are intentional markers in tests/selftest code, refactor into a Python fixture (.py allowed)." >&2
    echo "  3. OR: if markers are in prose/docstring/ASCII-art separators, ensure they do NOT start the line." >&2
    rm -f "$HITS_FILE"
    exit 1
fi

rm -f "$HITS_FILE"
echo "GATE_PASS: 0 unresolved git merge conflict markers in source code (cpp/hpp/h/c/cmake scope, scan: $SCAN_PATHS)"
echo "[INFO] ${GATE_NAME}: clean state across the source tree (line-start anchored <<<<<<</=======/>>>>>>> markers absent)"
exit 0
