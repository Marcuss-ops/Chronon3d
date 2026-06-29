#!/usr/bin/env bash
# tools/sdk/check_archive_canaries.sh — Step 3.5 (canary gate).
#
# Verifies that libchronon3d_sdk_impl.a in the install prefix:
#   (a) exists at all
#   (b) contains >= 2 .o files (marker TU + at least 1 subsystem)
#   (c) packs a demangled symbol table parseable via nm -C
#   (d) per canary entry in cmake/Chronon3DCanarySymbols.cmake (parsed
#       as 4-tuple "AREA|SYMBOL|GUARD|TARGET"), substring match on
#       the demangled symbol table
#   (e) honours guards CHRONON3D_ENABLE_TEXT, CHRONON3D_BUILD_DIAGNOSTICS,
#       CHRONON3D_BUILD_CONTENT — entries whose guard evaluates OFF are
#       skipped with a structured log line, NOT failed.
#
# Exit codes:
#   0 = canary gate passed (all required canaries present)
#   1 = gate failed (missing archive / ar t count / nm failure /
#                    missing required canary / catalogue parse failure)
#
# Env inputs (REQUIRED, exported by orchestrator):
#   SDK_PREFIX  — install prefix root (libchronon3d_sdk_impl.a lookup)
#   SDK_BUILD   — cmake build dir (CMakeCache.txt for guard values)
#   REPO_ROOT   — repo root (cmake/Chronon3DCanarySymbols.cmake lookup)
#
# Invocation pattern:  bash tools/sdk/check_archive_canaries.sh

# ── Source common.sh (idempotent) ─────────────────────────────────────
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

# ── Required env vars ─────────────────────────────────────────────────
: "${SDK_PREFIX:?SDK_PREFIX env var required (orchestrator must export)}"
: "${SDK_BUILD:?SDK_BUILD env var required (orchestrator must export — needed for CMakeCache.txt guard reads)}"
: "${REPO_ROOT:?REPO_ROOT env var required (orchestrator must export — needed for canary catalog lookup)}"

log "canary gate starting (SDK_PREFIX=$SDK_PREFIX, SDK_BUILD=$SDK_BUILD)"

# ── Boundary pre-conditions (config / targets / headers) + archive ──
impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
[[ -n "$impl_archive" ]] || fail "libchronon3d_sdk_impl.a not found in $SDK_PREFIX"

config_file="$(find "$SDK_PREFIX" -type f -name 'Chronon3DConfig.cmake' 2>/dev/null | head -1 || true)"
targets_file="$(find "$SDK_PREFIX" -type f -name 'Chronon3DTargets.cmake' 2>/dev/null | head -1 || true)"
headers_dir="$SDK_PREFIX/include/chronon3d"
[[ -n "$config_file"  ]] || fail "Chronon3DConfig.cmake not installed in $SDK_PREFIX"
[[ -n "$targets_file" ]] || fail "Chronon3DTargets.cmake not installed in $SDK_PREFIX"
[[ -d "$headers_dir"  ]] || fail "public headers not installed under $headers_dir"
header_count="$(find "$headers_dir" -maxdepth 6 -type f -name '*.hpp' 2>/dev/null | wc -l | tr -d ' ')"
(( header_count > 0 )) || fail "no .hpp files under $headers_dir (boundary broken)"
log "Boundary pre-conditions: config, targets, archive, $header_count headers"

# ── Read guards from CMakeCache.txt (with sensible defaults) ──────────
text_on="$(cache_var CHRONON3D_ENABLE_TEXT)";         : "${text_on:=ON}"
diag_on="$(cache_var CHRONON3D_BUILD_DIAGNOSTICS)";   : "${diag_on:=OFF}"
content_on="$(cache_var CHRONON3D_BUILD_CONTENT)";     : "${content_on:=ON}"
log "canary guards: text=$text_on diagnostics=$diag_on content=$content_on"

# ── Parse canary catalog (4-tuple: AREA|SYMBOL|GUARD|TARGET) ──────────
canary_file="$REPO_ROOT/cmake/Chronon3DCanarySymbols.cmake"
[[ -f "$canary_file" ]] || fail "canary catalog not found: $canary_file"
canary_entries="$(grep -oE '"[a-z_]+\|[a-zA-Z0-9_:]+\|[a-zA-Z0-9_]+\|[a-zA-Z0-9_]+"' "$canary_file" 2>/dev/null || true)"
[[ -n "$canary_entries" ]] || fail "no canary entries parsed from $canary_file (catalog must use 4-tuple AREA|SYMBOL|GUARD|TARGET format)"

canary_areas=()
canary_symbols=()
canary_guards=()
canary_targets=()
while IFS= read -r entry; do
    body="${entry#\"}"
    body="${body%\"}"
    IFS='|' read -r area symbol guard target <<<"$body"
    canary_areas+=("$area")
    canary_symbols+=("$symbol")
    canary_guards+=("$guard")
    canary_targets+=("$target")
done <<<"$canary_entries"
log "loaded ${#canary_areas[@]} canary entries from $canary_file"

# ── (a) ar t — count .o entries ──────────────────────────────────────
GATE_TMP="$(mktemp_dir chronon3d_install_gate)"
cleanup_register "$GATE_TMP"

ar_list="$GATE_TMP/ar.txt"
nm_dump="$GATE_TMP/nm.txt"

ar t "$impl_archive" | sort > "$ar_list" 2>/dev/null \
    || fail "ar t failed on $impl_archive (corrupt or unreadable archive)"
ar_count="$(wc -l < "$ar_list" | tr -d ' ')"
(( ar_count >= 2 )) || fail "archive contains only $ar_count .o files; need >= 2 (marker TU + at least 1 subsystem)"
log "ar t: $impl_archive :: $ar_count .o files (>= 2)"

# ── (b) nm -C — demangled symbol table (one-time per run) ────────────
nm -C "$impl_archive" > "$nm_dump" 2>/dev/null \
    || fail "nm -C failed on $impl_archive"

# ── (d)-(e) Walk canaries ─────────────────────────────────────────────
checked=0
missing=0
skipped=0
fail_list=""
for i in "${!canary_areas[@]}"; do
    area="${canary_areas[$i]}"
    symbol="${canary_symbols[$i]}"
    guard="${canary_guards[$i]}"
    target="${canary_targets[$i]}"

    skip_reason=""
    case "$guard" in
        always)                       : ;;
        CHRONON3D_ENABLE_TEXT)
            [[ "$text_on" == "ON" ]] || skip_reason="text_off (CHRONON3D_ENABLE_TEXT=$text_on)"
            ;;
        CHRONON3D_BUILD_DIAGNOSTICS)
            [[ "$diag_on" == "ON" ]] || skip_reason="diag_off (CHRONON3D_BUILD_DIAGNOSTICS=$diag_on)"
            ;;
        CHRONON3D_BUILD_CONTENT)
            [[ "$content_on" == "ON" ]] || skip_reason="content_off (CHRONON3D_BUILD_CONTENT=$content_on)"
            ;;
        *)
            log "WARN: unknown guard '$guard' for canary '$area'; treating as always"
            ;;
    esac

    if [[ -n "$skip_reason" ]]; then
        log "SKIP: canary $area [$target] ($skip_reason)"
        skipped=$((skipped + 1))
        continue
    fi

    if grep -F -q -- "$symbol" "$nm_dump"; then
        log "OK: canary $area [$target] :: $symbol"
        checked=$((checked + 1))
    else
        log "FAIL: canary $area [$target] :: '$symbol' not present in $impl_archive"
        missing=$((missing + 1))
        fail_list="${fail_list}${fail_list:+, }$area"
    fi
done

if (( missing > 0 )); then
    fail "$missing canary symbol(s) missing from archive: $fail_list.  " \
         "Rebuild with the matching CHRONON3D_ENABLE_* option enabled, or fix the catalog."
fi
log "Canary gate: $checked present, $skipped skipped (guard OFF), 0 missing"
exit 0
