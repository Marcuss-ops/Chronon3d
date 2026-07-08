#!/usr/bin/env bash
# ============================================================================
# tools/check_legacy_asset_prevalence.sh
#
# M1.7 Asset readiness legacy items grep audit (forward-only Step 4
# acceptance gate).
#
# Counts pre-Elimination prevalence of the 5 Asset legacy items A-E from the
# canonical user plan (verbatim from docs/tickets/TICKET-ASSET-READINESS.md):
#
#   A. Path raw sparsi (font_path / image_path / video_path / audio_path)
#      senza `AssetRef` owner. Pattern ristretto a content/ + src/scene/
#      (esclude include/chronon3d/ che ospita i field `.path` di FontSpec /
#      ImageSpec / VideoSpec / AudioSpec — non sono path raw sparsi,
#      sono campi del type system canonico).
#   B. Asset discovery render-time (resolve_handle / load_image /
#      decode_video / decode_audio / font_engine.load).
#   C. Fallback silenziosi (use_default_font / draw_black_rect / empty_frame
#      / placeholder / `continue; // missing`).
#   D. `catch (...) { MESSAGE("Render failed..."); return; }` nei test
#      readiness (multi-line pattern: rg --multiline if available, else
#      conservative per-file fallback).
#   E. Asset validation duplicata per-feature (TextPreflight /
#      ImagePreflight / VideoPreflight / AudioPreflight / FontPreflight).
#
# Step 4 acceptance gate: 0 hits per ogni item, total = 0.
# Vedi `docs/FOLLOWUP_TICKETS.md` row TICKET-ASSET-READINESS per il piano
# 4-step completo; questo script è solo il grep gate forward-only.
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

# Tool detection: ripgrep preferred (cleaner --type + --multiline); POSIX grep fallback.
if command -v rg >/dev/null 2>&1; then
    SEARCH_TOOL="rg"
elif command -v grep >/dev/null 2>&1; then
    SEARCH_TOOL="grep"
else
    echo "GATE_FAIL: neither rg nor grep available in PATH" >&2
    exit 1
fi

# count_hits pattern path [path ...] -> echoes integer (always exits 0)
# Uses ${result:-0} fallback to avoid double-output under set -o pipefail
# (when rg exits 1 for no matches, pipefail propagates and `|| echo "0"`
# fires AFTER awk already printed "0", producing "0\n0" in the variable).
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

# count_multiline_hits regex path [path ...] -> echoes integer (always exits 0)
# Used by AST_ITEM_D for catch (...) { MESSAGE ... return } (multi-line pattern).
# Uses ${result:-0} fallback (same fix as count_hits above).
count_multiline_hits() {
    local pattern="$1"; shift
    local paths=("$@")
    local result
    if [ "$SEARCH_TOOL" = "rg" ]; then
        result=$(rg --multiline --count-matches "$pattern" "${paths[@]}" \
            -g '*.cpp' 2>/dev/null \
            | awk -F: '{sum+=$NF} END {print sum+0}' \
            | head -1)
    else
        # POSIX fallback: heuristic — count files containing BOTH `catch (...)`
        # AND `MESSAGE` AND `return;` in the same file (coarse but portable).
        # Returns the number of files that match (over-estimation preferred to
        # under-estimation for a forward-only pre-Elimination snapshot).
        result=$(grep -rElZ 'catch[[:space:]]*\([[:space:]]*\.\.\.[[:space:]]*\)' "${paths[@]}" \
            --include='*.cpp' 2>/dev/null \
            | xargs -0 grep -lE 'MESSAGE' 2>/dev/null \
            | xargs -I{} grep -lE 'return;' {} 2>/dev/null \
            | wc -l | tr -d ' ')
    fi
    echo "${result:-0}"
}

printf "═══════ M1.7 Asset legacy items grep audit ════\n"
printf "Forward-only Step 4 acceptance gate. Target: 0 hits per item.\n"
printf "Search tool: %s\n" "$SEARCH_TOOL"
printf "REPO_ROOT:   %s\n" "$REPO_ROOT"
printf "\n"

total=0
names=(AST_ITEM_A AST_ITEM_B AST_ITEM_C AST_ITEM_D AST_ITEM_E)

# ── AST_ITEM_A: path raw sparsi (font_path / image_path / video_path / audio_path)
#    Scope ristretto a content/ + src/scene/ (esclude include/chronon3d/ che
#    ospita i field `.path` dei type system canonici come FontSpec.path).
#    Tightened: excludes struct field accesses like `.font_path = "..."`
#    (canonical API) by requiring no preceding dot/alphanumeric. Only catches
#    bare variable declarations `std::string font_path{...}` etc.
c=$(count_hits '(^|[^a-zA-Z0-9_.])(font_path|image_path|video_path|audio_path)\b' \
    content src/scene)
printf "%-12s %8d hits   (scope: content/ + src/scene/ — exclude include/chronon3d/ .path field reads)\n" "${names[0]}" "$c"
total=$((total + c))

# ── AST_ITEM_B: asset discovery render-time
#    Scope narrowed: excludes src/backends/ (those are canonical backend
#    IMPLEMENTATIONS — load_image, resolve_handle are the actual loading
#    mechanism, not legacy discovery). Only src/scene/ + content/ are
#    legacy discovery sites that should migrate to AssetPreflightResolver.
c=$(count_hits '(resolve_handle|load_image|decode_video|decode_audio|font_engine\.load)' \
    src/scene content)
printf "%-12s %8d hits\n" "${names[1]}" "$c"
total=$((total + c))

# ── AST_ITEM_C: fallback silenziosi
#    Tightened: drops 'placeholder' (false positives in comments/UI code).
#    Requires function-call syntax for use_default_font/draw_black_rect.
#    Requires actual return statement for empty_frame.
c1=$(count_hits '\b(use_default_font|draw_black_rect)[[:space:]]*\(' \
    src content tests)
c2=$(count_hits 'return[[:space:]]+empty_frame;' \
    src content tests)
c3=$(count_hits 'continue;[[:space:]]*//[[:space:]]*missing' \
    src content tests)
c=$((c1 + c2 + c3))
printf "%-12s %8d hits   (func-call %d + return-empty %d + continue-missing %d)\n" "${names[2]}" "$c" "$c1" "$c2" "$c3"
total=$((total + c))

# ── AST_ITEM_D: catch (...) { MESSAGE ... return } nei test readiness
c=$(count_multiline_hits 'catch\s*\(\s*\.\.\.\s*\)\s*\{[^}]*MESSAGE[^}]*return' \
    tests)
printf "%-12s %8d hits   (multi-line pattern)\n" "${names[3]}" "$c"
total=$((total + c))

# ── AST_ITEM_E: Asset validation duplicata per-feature
#    Narrowed: targets only the 5 legacy per-feature preflight classes
#    (TextPreflight / ImagePreflight / VideoPreflight / AudioPreflight /
#    FontPreflight). Excludes RenderPreflight which is a legitimate
#    authoring-time validation framework (not a duplicate).
c=$(count_hits 'class[[:space:]]+(Text|Image|Video|Audio|Font)Preflight\b' \
    src include)
printf "%-12s %8d hits\n" "${names[4]}" "$c"
total=$((total + c))

printf "\n"
printf "TOTAL hits across 5 Asset legacy items: %d\n" "$total"
printf "Acceptance gate (Step 4 DONE): 0 per ogni item, total = 0.\n"
