#!/usr/bin/env bash
# ============================================================================
# tools/audit_incomplete_type_pattern.sh
# ============================================================================
# FU4 rot pattern hardening audit (TICKET-GATE-10-PHASE-4-BLACK-FU4 closure
# lineage).  Pattern caught:
#
#     std::make_shared<T>(...) where T is ONLY forward-declared
#     (e.g. `class T;`) in the SDK public umbrella header under
#     include/chronon3d/, with no FULL class/struct definition reachable
#     in the consumer TU.  std::make_shared's deleter uses an incomplete
#     type, producing runtime UB that compiles cleanly — this is exactly
#     how FU4 manifested on `chronon3d::TextRunShape` at main@c9415e5b
#     (fix pivot = main@0b365354, explicit consumer-side include of the
#     full-def sub-header).
#
# Detection rule (one-line per extracted type):
#
#     1. Scan $SCAN_PATHS for `std::make_shared<TYPE>(` call sites and
#        collect the unique set of TYPE identifiers (with namespace
#        qualifiers `c3d::`/`chronon3d::` and inline `const` stripped).
#     2. For each TYPE, locate candidate umbrella header(s) under
#        $UMBRELLA_ROOT (default: include/chronon3d).  A candidate is
#        any .hpp file that contains `class TYPE` or `struct TYPE`.
#     3. Per candidate, classify as FULL-DEF if EITHER
#          (A) `(class|struct) TYPE` on the SAME line is followed by
#              `{` (body opener) OR `:` (inheritance colon), OR
#          (B) `(class|struct) TYPE` is on a line of its own and the
#              NEXT non-blank/non-comment line begins with `{` or `:`
#              (multi-line declaration style).
#        Else classify as FORWARD-DECL (line terminates with `;`).
#     4. The first candidate with FULL-DEF wins → OK for that TYPE.
#        If NO candidate has FULL-DEF → BROKEN for that TYPE.
#
# Override via env vars:
#   INCOMPLETE_TYPE_SCAN_PATHS     scan roots (default below)
#   INCOMPLETE_TYPE_UMBRELLA_ROOT  umbrella lookup root (default below)
#
# Exit codes:
#   0 = all std::make_shared<T> have fully-defined umbrella headers
#   1 = at least one TYPE is BROKEN (forward-decl-only rot)
#   2 = script internal error
# ============================================================================
set -euo pipefail

# ── Resolve repository root ────────────────────────────────────────────
if [ -n "${INCOMPLETE_TYPE_SCAN_REPO_OVERRIDE:-}" ]; then
    REPO_ROOT="$INCOMPLETE_TYPE_SCAN_REPO_OVERRIDE"
else
    REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
fi
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

# ── Defaults (configurable via env) ────────────────────────────────────
SCAN_PATHS_DEFAULT="tests/install_consumer/main.cpp tests/install_consumer/"
UMBRELLA_ROOT_DEFAULT="include/chronon3d"
SCAN_PATHS="${INCOMPLETE_TYPE_SCAN_PATHS:-$SCAN_PATHS_DEFAULT}"
UMBRELLA_ROOT="${INCOMPLETE_TYPE_UMBRELLA_ROOT:-$UMBRELLA_ROOT_DEFAULT}"

# ── Banner ──────────────────────────────────────────────────────────────
echo "=== incomplete-type pattern audit (FU4 rot preventive) ==="
echo "scan paths : $SCAN_PATHS"
echo "umbrella   : $UMBRELLA_ROOT"
echo ""

# ── 1. Extract TYPES used in `std::make_shared<TYPE>(` ────────────────
#    Strip: leading `std::make_shared<`, inline `const`, and namespace
#    qualifier `c3d::` / `chronon3d::`.  Dedup via sort -u.  Empty stdin
#    → empty TYPES (handled below).
TYPES=""
# `set +e` is local to this single command (|| true) so a missing scan
# path doesn't terminate the audit with -e.
set +e
# NOTE: sed delimiter is `@` (not the default `/`) because the regex
# pattern alternation `(c3d|chronon3d)` contains `|` — using `|` as the
# sed delimiter would collide with the alternation pipes and emit
# "sed: -e expression #1, char N: unknown option to `s`".  `@` is not a
# character that appears in either std::make_shared<>() call sites OR
# the captured identifiers.
TYPES=$(grep -RhoE \
        'std::make_shared<[[:space:]]*(const[[:space:]]+)?((c3d|chronon3d)::)?[A-Za-z_][A-Za-z0-9_]*[[:space:]]*>' \
        $SCAN_PATHS 2>/dev/null \
        | sed -E 's@.*std::make_shared<[[:space:]]*(const[[:space:]]+)?((c3d|chronon3d)::)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*>$@\4@' \
        | sort -u)
set -e

if [ -z "$TYPES" ]; then
    echo "[OK] no std::make_shared<T> call sites in scan paths"
    echo "=== AUDIT PASSED (no rot risk surface present) ==="
    exit 0
fi

TYPE_COUNT=$(printf '%s\n' "$TYPES" | wc -l | tr -d '[:space:]')
echo "extracted types: $TYPE_COUNT"
echo ""

# ── 2 + 3. Per-TYPE umbrella resolution + full-def classification ─────
FAILED=0
for TYPE in $TYPES; do
    echo "-- checking type: $TYPE"

    # Broaden scope to all .hpp under the umbrella root, not just files
    # that mention CLASS/STRUCT TYPE — that path may falsely miss types
    # whose umbrella uses `using TYPE = ...;` style aliases (rare in
    # this repo but cheap to handle).  Primary check: grep -RInlE
    # `\b(class|struct)\s+TYPE\b` against the umbrella root.
    CANDIDATE_FILES=""
    set +e
    CANDIDATE_FILES=$(grep -RInlE \
        "\b(class|struct)[[:space:]]+${TYPE}\b" \
        "$UMBRELLA_ROOT" 2>/dev/null | sort -u)
    set -e

    if [ -z "$CANDIDATE_FILES" ]; then
        echo "  [BROKEN] no umbrella header declares $TYPE in $UMBRELLA_ROOT"
        FAILED=1
        continue
    fi

    TYPE_OK=0
    for CAND in $CANDIDATE_FILES; do
        # ── (A) Same-line opener: `(class|struct) TYPE ... {` or `:`
        set +e
        SAME_LINE=$(grep -cE \
            "(class|struct)[[:space:]]+${TYPE}\b[[:space:]]*[\{:]" \
            "$CAND")
        set -e
        [ -z "$SAME_LINE" ] && SAME_LINE=0

        # ── (B) Multi-line opener: declaration line ends with TYPE
        #        alone, next non-blank line starts with `{` or `:`.
        set +e
        MULTI_LINE=$(grep -nE \
            "(class|struct)[[:space:]]+${TYPE}\b[[:space:]]*$" \
            "$CAND" \
            | awk -F: '{print $1}' \
            | while read -r lineno; do
                # Use sed (more portable than awk for line fetch) to grab the next line.
                next_line=$(sed -n "$((lineno+1))p" "$CAND" 2>/dev/null || true)
                case "$next_line" in
                    [[:space:]]*[\{:]*) echo "match" ;;
                esac
            done | wc -l)
        set -e
        [ -z "$MULTI_LINE" ] && MULTI_LINE=0

        # ── Forward-decl count (for diagnostic context only; decision
        #     is based on SAME_LINE/MULTI_LINE being ≥1).
        set +e
        FWD_DECL=$(grep -cE \
            "(class|struct)[[:space:]]+${TYPE}\b[[:space:]]*;" \
            "$CAND")
        set -e
        [ -z "$FWD_DECL" ] && FWD_DECL=0

        if [ "$SAME_LINE" -gt 0 ] || [ "$MULTI_LINE" -gt 0 ]; then
            TYPE_OK=1
            # Compact diagnostic: basename + counters.  Truncate to a
            # single line per file to keep multi-TYPE reports scannable.
            echo "  [ OK ]  $TYPE — $(basename "$CAND") (same=$SAME_LINE multi=$MULTI_LINE fwd=$FWD_DECL)"
            break
        fi
    done

    if [ "$TYPE_OK" -eq 0 ]; then
        echo "  [BROKEN] std::make_shared<$TYPE>(): no umbrella candidate in $UMBRELLA_ROOT contains a full definition"
        echo "            (forward-decl-only rot — FU4-style risk)"
        FAILED=1
    fi
done

echo ""
if [ "$FAILED" -ne 0 ]; then
    BROKEN_COUNT=$(echo "$TYPES" | wc -l)
    echo "=== AUDIT FAILED: forward-decl-only umbrella rot (FU4-style) ==="
    echo ""
    echo "Fix: for each BROKEN TYPE, ensure \$UMBRELLA_ROOT/<path>/<TYPE>.hpp"
    echo "(or equivalent umbrella) contains either:"
    echo "    struct TYPE { ... };     // full definition"
    echo "    class TYPE : Base { ... };    // inheritance opener (full def)"
    echo "and NOT just:"
    echo "    class TYPE;             // forward declaration only"
    echo ""
    echo "TYPE count checked: ${TYPE_COUNT:-0}"
    exit 1
fi

echo "=== AUDIT PASSED: all std::make_shared<T> have fully-defined umbrellas ==="
echo "TYPE count checked: $TYPE_COUNT"
exit 0
