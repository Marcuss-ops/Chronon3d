#!/usr/bin/env bash
# tools/check_legacy_text_pipeline.sh
# ─────────────────────────────────────────────────────────────────────
# P1 #4 — Dual text pipeline architecture gate.
#
# `rasterize_text_to_bl_image` is retired: any production declaration or
# callsite is now a regression. TextLayoutEngine::layout remains census-
# tracked until its own migration closes.
#
# EXIT CODES:
#   0 = no unauthorized production callsites detected (PASS)
#   1 = unauthorized production callsite(s) found (FAIL)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="${BOUNDARY_CHECK_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

FAILED=0
SCAN_PATHS='src include'

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

# Empty by design: the rasterize_text_to_bl_image ABI and implementation
# were retired after production, CLI, and test callers reached zero.
R2BI_WL=()

TLE_WL=(
    'include/chronon3d/backends/text/text_layout_engine\.hpp'
)

echo "=== Legacy Text Pipeline Gate (P1 #4 — census gate) ==="

echo -n "  [1/3] rasterize_text_to_bl_image           ... "
raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\brasterize_text_to_bl_image\b' $SCAN_PATHS 2>/dev/null || true)
code_only=$(echo "$raw" | filter_code_only 'rasterize_text_to_bl_image' || true)
new_hits_file=$(mktemp)
filter_whitelist 'rasterize_text_to_bl_image' R2BI_WL > "$new_hits_file" <<< "$code_only" || true
new_hits=$(cat "$new_hits_file")
rm -f "$new_hits_file"

if [ -n "$new_hits" ]; then
    echo "FAIL"
    echo "  Retired rasterizer symbol reintroduced:"
    echo "$new_hits"
    echo "  → Use the canonical TextRunShape / draw_text_run pipeline."
    FAILED=1
else
    echo "PASS"
fi

echo -n "  [2/3] TextLayoutEngine::layout             ... "
raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bTextLayoutEngine::layout\b' $SCAN_PATHS 2>/dev/null || true)
code_only=$(echo "$raw" | filter_code_only 'TextLayoutEngine::layout' || true)
new_hits_file=$(mktemp)
filter_whitelist 'TextLayoutEngine::layout' TLE_WL > "$new_hits_file" <<< "$code_only" || true
new_hits=$(cat "$new_hits_file")
rm -f "$new_hits_file"

if [ -n "$new_hits" ]; then
    echo "FAIL"
    echo "  NEW unauthorized production callsites:"
    echo "$new_hits"
    echo "  → Migrate to compile_text_layout."
    FAILED=1
else
    echo "PASS"
fi

echo -n "  [3/3] Census cross-verify (tests+apps)     ... "
t_r2bi=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\brasterize_text_to_bl_image\b' tests/ 2>/dev/null | wc -l || true)
a_r2bi=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\brasterize_text_to_bl_image\b' apps/ 2>/dev/null | wc -l || true)
t_tle=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bTextLayoutEngine::layout\b' tests/ 2>/dev/null | wc -l || true)
a_tle=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bTextLayoutEngine::layout\b' apps/ 2>/dev/null | wc -l || true)

echo "PASS (r2bi:${t_r2bi}t/${a_r2bi}a  tle:${t_tle}t/${a_tle}a)"

echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Legacy text pipeline gate FAILED ==="
    exit 1
fi
echo "=== Legacy text pipeline gate PASSED ==="
exit 0
