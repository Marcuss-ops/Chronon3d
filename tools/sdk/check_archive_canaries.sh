#!/usr/bin/env bash
# Validate canonical subsystem canaries in the installed SDK archive.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${SDK_BUILD:?SDK_BUILD env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required}"

impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
[[ -n "$impl_archive" ]] || fail "libchronon3d_sdk_impl.a not found in $SDK_PREFIX"
[[ -f "$SDK_BUILD/CMakeCache.txt" ]] || fail "CMakeCache.txt missing in $SDK_BUILD"
[[ -d "$SDK_PREFIX/include/chronon3d" ]] || fail "public headers missing from install prefix"

text_on="$(cache_var CHRONON3D_ENABLE_TEXT)"; : "${text_on:=ON}"
diag_on="$(cache_var CHRONON3D_ENABLE_DIAGNOSTICS)"; : "${diag_on:=OFF}"
log "canary guards: text=$text_on diagnostics=$diag_on"

canary_file="$REPO_ROOT/cmake/Chronon3DCanarySymbols.cmake"
[[ -f "$canary_file" ]] || fail "canary catalog not found: $canary_file"
canary_entries="$(grep -oE '"[a-z_]+\|[a-zA-Z0-9_:]+\|[a-zA-Z0-9_]+\|[a-zA-Z0-9_]+"' "$canary_file" || true)"
[[ -n "$canary_entries" ]] || fail "no canary entries parsed from $canary_file"

GATE_TMP="$(mktemp_dir chronon3d_install_gate)"
cleanup_register "$GATE_TMP"
ar_before="$GATE_TMP/ar_before.txt"
ar_after="$GATE_TMP/ar_after.txt"
nm_dump="$GATE_TMP/nm.txt"

ar t "$impl_archive" > "$ar_before" || fail "ar t failed on $impl_archive"
ar_count="$(wc -l < "$ar_before" | tr -d ' ')"
(( ar_count >= 2 )) || fail "archive contains only $ar_count object entries"

nm -C "$impl_archive" > "$nm_dump" || fail "nm -C failed on $impl_archive"
ar t "$impl_archive" > "$ar_after" || fail "post-nm ar t failed on $impl_archive"
ar_count_after="$(wc -l < "$ar_after" | tr -d ' ')"
(( ar_count_after >= 1 )) || fail "post-nm archive listing is empty"
[[ "$ar_count_after" == "$ar_count" ]] \
    || fail "archive object count drifted across nm: $ar_count -> $ar_count_after"

checked=0
skipped=0
missing=0
fail_list=""
while IFS= read -r entry; do
    body="${entry#\"}"
    body="${body%\"}"
    IFS='|' read -r area symbol guard target <<<"$body"

    skip_reason=""
    case "$guard" in
        always) ;;
        CHRONON3D_ENABLE_TEXT)
            [[ "$text_on" == "ON" ]] || skip_reason="text_off"
            ;;
        CHRONON3D_ENABLE_DIAGNOSTICS)
            [[ "$diag_on" == "ON" ]] || skip_reason="diagnostics_off"
            ;;
        *) fail "unknown canary guard '$guard' for $area" ;;
    esac

    if [[ -n "$skip_reason" ]]; then
        log "SKIP: canary $area [$target] ($skip_reason)"
        skipped=$((skipped + 1))
        continue
    fi

    case "$symbol" in
        arch:ar_t_post_nm_non_empty)
            log "OK: structural $area [$target]"
            checked=$((checked + 1))
            ;;
        arch:*)
            fail "unknown structural canary '$symbol' for $area"
            ;;
        *)
            if grep -F -q -- "$symbol" "$nm_dump"; then
                log "OK: canary $area [$target] :: $symbol"
                checked=$((checked + 1))
            else
                log "FAIL: canary $area [$target] :: $symbol"
                missing=$((missing + 1))
                fail_list="${fail_list}${fail_list:+, }$area"
            fi
            ;;
    esac
done <<<"$canary_entries"

(( missing == 0 )) || fail "$missing canary(s) missing: $fail_list"
log "Canary gate: $checked present, $skipped skipped, 0 missing"
