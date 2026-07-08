#!/usr/bin/env bash
# ============================================================================
# tools/check_legacy_timeline_prevalence.sh
#
# M1.7 Sequence legacy items grep audit (forward-only Step 4 acceptance gate).
#
# Counts pre-Elimination prevalence of the 5 Sequence legacy items A-E from
# the canonical user plan (verbatim from
# docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md):
#
#   A. `if (frame ...)` sparsi nei content + `frame>=start && frame<end`
#      + `layer.from` / `.duration()` round-trips (mixed pattern).
#   B. Animator/sampler che legge `global_frame` / `ctx.frame` /
#      `frame_context.frame` invece di `local_frame`.
#   C. `Layer.from / Layer.duration` decisi dal render graph (incluso
#      skip-path `if (frame < layer.from)`).
#   D. `duration = 0` o `duration = 1` (numeri magici usati come sentinella
#      "statico").
#   E. 5 coordinate temporali duplicate (composition / layer / sequence /
#      animator / video source frame).
#
# Step 4 acceptance gate: 0 hits per ogni item, total = 0.
# Vedi `docs/FOLLOWUP_TICKETS.md` row TICKET-SEQUENCE-LOCAL-FRAME per il
# piano 4-step completo; questo script è solo il grep gate forward-only.
#
# Exit codes:
#   0 = audit completed successfully (output printed; counts may be 0 or >0)
#   1 = MISSING_TOOL (neither rg nor grep available) OR invalid REPO_ROOT
# ============================================================================

set -u  # NOT -e: count_hits pipeline may exit 1 (no matches) without aborting
set -o pipefail

REPO_ROOT="${CHECK_PREVALENCE_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
if [ ! -d "$REPO_ROOT" ]; then
    echo "GATE_FAIL: invalid REPO_ROOT='$REPO_ROOT'" >&2
    exit 1
fi
cd "$REPO_ROOT"

# Tool detection: ripgrep preferred (cleaner --type filtering); POSIX grep fallback.
if command -v rg >/dev/null 2>&1; then
    SEARCH_TOOL="rg"
elif command -v grep >/dev/null 2>&1; then
    SEARCH_TOOL="grep"
else
    echo "GATE_FAIL: neither rg nor grep available in PATH" >&2
    exit 1
fi

# count_hits pattern path [path ...] -> echoes integer (always exits 0)
# Per-file matching counts (ripgrep --count-matches), summed across files.
# `|| echo "0"` fallback ensures we never propagate the no-match exit 1
# into set +o pipefail.
count_hits() {
    local pattern="$1"; shift
    local paths=("$@")
    local result
    if [ "$SEARCH_TOOL" = "rg" ]; then
        result=$(rg --count-matches "$pattern" "${paths[@]}" \
            -g '*.cpp' -g '*.hpp' 2>/dev/null \
            | awk -F: '{sum+=$NF} END {print sum+0}' \
            | head -1)
    else
        result=$(grep -rE "$pattern" "${paths[@]}" \
            --include='*.cpp' --include='*.hpp' 2>/dev/null | wc -l | tr -d ' ')
    fi
    echo "${result:-0}"
}

printf "═══════ M1.7 Sequence legacy items grep audit ═══════\n"
printf "Forward-only Step 4 acceptance gate. Target: 0 hits per item.\n"
printf "Search tool: %s\n" "$SEARCH_TOOL"
printf "REPO_ROOT:   %s\n" "$REPO_ROOT"
printf "\n"

total=0
names=(SEQ_ITEM_A SEQ_ITEM_B SEQ_ITEM_C SEQ_ITEM_D SEQ_ITEM_E)

# ── SEQ_ITEM_A: layer.from / layer.duration raw access in content code.
#    Scope: content/ ONLY (not src/scene/ or src/render_graph/ which are
#    infrastructure and legitimately access the Layer data model).
#    Camera orchestration code (shot_timeline, camera_motion_applier) uses
#    frame >= start_frame legitimately and is excluded.
c=$(count_hits '\blayer\.from\b|\blayer\.duration\(\)' \
    content)
printf "%-12s %8d hits\n" "${names[0]}" "$c"
total=$((total + c))

# ── SEQ_ITEM_B: animator/sampler reads global_frame / ctx.frame
#    Tightened: only catches sample() calls using global frame (the legacy
#    pattern), not all ctx.frame reads (which include legitimate camera
#    orchestrator code).
c=$(count_hits '\bsample\([[:space:]]*(ctx\.frame|frame_context\.frame|global_frame)\b' \
    content src/text src/animation)
printf "%-12s %8d hits\n" "${names[1]}" "$c"
total=$((total + c))

# ── SEQ_ITEM_C: layer.from / layer.duration temporal coordination in
#    content-level scene wiring (not in render graph infrastructure).
#    Scope: content/ only. The render graph builder legitimately reads
#    layer.from/layer.duration to construct graph nodes — that is
#    architectural, not legacy.
c=$(count_hits 'if[[:space:]]*\([[:space:]]*frame[[:space:]]*<[[:space:]]*layer\.from' \
    content)
printf "%-12s %8d hits\n" "${names[2]}" "$c"
total=$((total + c))

# ── SEQ_ITEM_D: duration = 1 (or 0) magic
#    Tightened: excludes `.duration = Frame{1}` struct assignments (canonical
#    API) by requiring no preceding dot/alphanumeric. Only catches standalone
#    `duration = 0` or `duration = 1` variable assignments.
c=$(count_hits '(^|[^a-zA-Z0-9_.])duration[[:space:]]*=[[:space:]]*[01]\b' \
    content src/scene)
printf "%-12s %8d hits\n" "${names[3]}" "$c"
total=$((total + c))

# ── SEQ_ITEM_E: 5 coordinate temporali duplicate
#    Tightened: word boundaries exclude function names like
#    `render_composition_frame` and `freeze_frame`.
c=$(count_hits '\b(composition_frame|layer_frame|sequence_frame|animator_frame|source_frame)\b' \
    content src)
printf "%-12s %8d hits\n" "${names[4]}" "$c"
total=$((total + c))

printf "\n"
printf "TOTAL hits across 5 Sequence legacy items: %d\n" "$total"
printf "Acceptance gate (Step 4 DONE): 0 per ogni item, total = 0.\n"
