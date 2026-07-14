#!/usr/bin/env bash
# tools/check_legacy_text_pipeline.sh
# ─────────────────────────────────────────────────────────────────────
# P1 #4 — Dual text pipeline (TextShape vs TextRun) architecture gate.
#
# Blocks NEW production callsites of the legacy text pipeline functions
# outside the known, census-tracked whitelist.  The census lives in
# `docs/tickets/TICKET-P1-ACTION-PLAN.md` (P1 #4 section).
#
# EXIT CODES:
#   0 = no NEW production callsites detected (PASS)
#   1 = new unauthorized production callsite(s) found (FAIL)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="${BOUNDARY_CHECK_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

FAILED=0
SCAN_PATHS='src include'

# ── Comment-strip filter (same pattern as check_architecture_boundaries.sh) ──
#
# Reads grep -Rn output (format: path:line:content) from stdin.
# Drops pure-comment lines AND lines where the symbol appears ONLY in a
# trailing `//` comment.  Block comments (`/* ... */`) are NOT stripped
# — those are rare and should be flagged for human review.
filter_code_only() {
    local sym="$1"
    awk -F: -v sym="$sym" '
        {
            rest = ""
            for (i = 3; i <= NF; i++) {
                rest = rest (i == 3 ? "" : ":") $i
            }
            if (rest ~ /^[[:space:]]*(\/\/\/|\/\/|\/\*|\*)/) next
            pre = rest
            sub(/\/\/.*/, "", pre)
            if (pre !~ sym) next
            print
        }
    '
}

# ── Whitelist filter ──────────────────────────────────────────────────
#
# Reads grep -Rn output from stdin.  Prints lines whose PATH does NOT
# match any entry in the whitelist array.  Exits 0 if no new hits,
# 1 if new hits were found.
#
# Usage: echo "$hits" | filter_whitelist SYM_NAME WHITELIST_ARRAY_NAME

filter_whitelist() {
    local sym_name="$1"
    local -n wl_ref="$2"
    local new_hits=0

    while IFS= read -r line; do
        [ -z "$line" ] && continue
        local path="${line%%:*}"
        local matched=0
        for wl_re in "${wl_ref[@]}"; do
            if [[ "$path" =~ $wl_re ]]; then
                matched=1
                break
            fi
        done
        if [[ "$matched" -eq 0 ]]; then
            echo "    $line"
            new_hits=1
        fi
    done
    return $new_hits
}

# ── Whitelists (see P1 #4 census in TICKET-P1-ACTION-PLAN.md) ─────────

R2BI_WL=(
    'src/backends/text/text_rasterizer_render\.cpp'
    'src/backends/text/text_rasterizer_ink\.cpp'
    'include/chronon3d/backends/text/text_rasterizer_utils\.hpp'
    'src/backends/software/processors/text/software_text_processor\.cpp'
    'src/scene/model/render_node_factory\.cpp'             # comment only
    'include/chronon3d/presets/motion_object\.hpp'         # comment only
    'include/chronon3d/text/rich_text\.hpp'                # comment only
)

TLE_WL=(
    'include/chronon3d/backends/text/text_layout_engine\.hpp'
    'src/backends/text/text_rasterizer_render\.cpp'
    'include/chronon3d/text/rich_text\.hpp'
    'src/backends/software/processors/text/software_text_processor\.cpp'  # comment (deprecation notice)
)

# ═══════════════════════════════════════════════════════════════════════
echo "=== Legacy Text Pipeline Gate (P1 #4 — census gate) ==="

# ── Check 1: rasterize_text_to_bl_image ───────────────────────────────
echo -n "  [1/3] rasterize_text_to_bl_image           ... "

raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\brasterize_text_to_bl_image\b' $SCAN_PATHS 2>/dev/null || true)
code_only=$(echo "$raw" | filter_code_only 'rasterize_text_to_bl_image' || true)
# Use temp file to avoid subshell exit-code loss (CR review feedback)
new_hits_file=$(mktemp)
filter_whitelist 'rasterize_text_to_bl_image' R2BI_WL > "$new_hits_file" <<< "$code_only" || true
new_hits=$(cat "$new_hits_file")
rm -f "$new_hits_file"

if [ -n "$new_hits" ]; then
    echo "FAIL"
    echo "  NEW unauthorized production callsites:"
    echo "$new_hits"
    echo "  → Move to tests/, apps/, or migrate to draw_text_run."
    FAILED=1
else
    echo "PASS"
fi

# ── Check 2: TextLayoutEngine::layout ─────────────────────────────────
echo -n "  [2/3] TextLayoutEngine::layout             ... "

raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bTextLayoutEngine::layout\b' $SCAN_PATHS 2>/dev/null || true)
code_only=$(echo "$raw" | filter_code_only 'TextLayoutEngine::layout' || true)
# Use temp file to avoid subshell exit-code loss (CR review feedback)
new_hits_file=$(mktemp)
filter_whitelist 'TextLayoutEngine::layout' TLE_WL > "$new_hits_file" <<< "$code_only" || true
new_hits=$(cat "$new_hits_file")
rm -f "$new_hits_file"

if [ -n "$new_hits" ]; then
    echo "FAIL"
    echo "  NEW unauthorized production callsites:"
    echo "$new_hits"
    echo "  → Move to tests/, apps/, or migrate to compile_text_layout."
    FAILED=1
else
    echo "PASS"
fi

# ── Check 3: Census sanity cross-verify ───────────────────────────────
echo -n "  [3/3] Census cross-verify (tests+apps)     ... "
# FIX: `set -e` + `set -o pipefail` (top of file) make the bare `grep |
# wc -l` pipelines fail with exit 1 whenever grep returns 0 matches —
# which is the canonical post-P1-7-Chore-1 state (all test callers
# retired).  The Census cross-verify is INFORMATIONAL only (the FAIL
# is decided by [1/3] and [2/3] above); the bug manifested as a
# false-FAIL of the whole script.  Append `|| true` to swallow
# pipefail-induced exit-1 on 0-match grep, preserving the count of
# matching files (it goes through `$(... | wc -l)` which still
# substitutes the numeric count regardless of the pipeline exit).
t_r2bi=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\brasterize_text_to_bl_image\b' tests/ 2>/dev/null | wc -l || true)
a_r2bi=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\brasterize_text_to_bl_image\b' apps/ 2>/dev/null | wc -l || true)
t_tle=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bTextLayoutEngine::layout\b' tests/ 2>/dev/null | wc -l || true)
a_tle=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bTextLayoutEngine::layout\b' apps/ 2>/dev/null | wc -l || true)

echo "PASS (r2bi:${t_r2bi}t/${a_r2bi}a  tle:${t_tle}t/${a_tle}a)"

# ═══════════════════════════════════════════════════════════════════════
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Legacy text pipeline gate FAILED ==="
    exit 1
fi
echo "=== Legacy text pipeline gate PASSED ==="
exit 0
