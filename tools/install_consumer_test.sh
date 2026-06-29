#!/usr/bin/env bash
# tools/install_consumer_test.sh
# ─────────────────────────────────────────────────────────────────────
# End-to-end install boundary CI test for Chronon3D SDK.
#
# Pipeline:
#   1. Configure + build the SDK to a temp dir.
#   2. `cmake --install` into an isolated temp prefix.
#   3. Verify boundary artifacts (config, targets, archive, headers).
#   4. Configure + build + run the standalone consumer project at
#      `tests/install_consumer/` against the temp prefix.
#   5. Validate that the consumer stdout contains [BOUNDARY-OK]
#      and that it produced a non-empty PNG output.
#   6. Emit a single-line JSON summary for CI log scraping.
#
# Wired into CTest via `add_test(NAME install_consumer_ci COMMAND …)`
# in the top-level CMakeLists.txt, gated by the cache var
# CHRONON3D_BUILD_INSTALL_CONSUMER_TEST (enabled by the linux-ci preset).
#
# Exit codes:
#   0 = boundary validated
#   1 = boundary broken
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

# ────────────────────────── Configuration ────────────────────────────
PRESET="${CHRONON3D_INSTALL_TEST_PRESET:-linux-ci}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_ROOT="${TMPDIR:-/tmp}"

log() { printf "[install_consumer_test] %s\n" "$*" >&2; }

# ────────────────────────── Tooling guard ─────────────────────────────
command -v cmake >/dev/null || { log "FAIL: cmake not on PATH"; exit 1; }
cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
cmake_major="$(echo "$cmake_version" | cut -d. -f1)"
cmake_minor="$(echo "$cmake_version" | cut -d. -f2)"
if (( cmake_major < 3 || (cmake_major == 3 && cmake_minor < 25) )); then
    log "FAIL: cmake >=3.25 required (have $cmake_version)"
    exit 1
fi
log "cmake $cmake_version at $(command -v cmake)"
log "repo root  : $REPO_ROOT"
log "preset     : $PRESET"

# ────────────────────────── Temp isolation ────────────────────────────
SDK_BUILD=""
SDK_PREFIX=""
CONS_BUILD=""
GATE_TMP=""
cleanup() {
    local rc=$?
    [[ -n "$SDK_BUILD" ]] && rm -rf "$SDK_BUILD"
    [[ -n "$SDK_PREFIX" ]] && rm -rf "$SDK_PREFIX"
    [[ -n "$CONS_BUILD" ]] && rm -rf "$CONS_BUILD"
    [[ -n "$GATE_TMP"  ]] && rm -rf "$GATE_TMP"
    exit "$rc"
}
trap cleanup EXIT

SDK_BUILD="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_sdk_build.XXXXXX")"
SDK_PREFIX="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_prefix.XXXXXX")"
CONS_BUILD="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_build.XXXXXX")"
log "temp sdk build : $SDK_BUILD"
log "temp install   : $SDK_PREFIX"
log "temp consumer  : $CONS_BUILD"

# ────────────────────────── Step 1: build SDK ─────────────────────────
log "Configuring SDK at $SDK_BUILD (preset=$PRESET)"
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" 1>&2

log "Building SDK target 'chronon3d_sdk_impl'"
cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl 1>&2

# ────────────────────────── Step 2: install ───────────────────────────
log "Installing SDK into $SDK_PREFIX"
cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2

# ────────────────────────── Step 2.5: feature-OFF ghost sweep ───────
# Activated by CHRONON3D_INSTALL_TEST_GHOST_SWEEP=1 env var.  When
# ON, reconfigure the SAME $SDK_BUILD with DIAG=OFF + CONTENT=OFF,
# rebuild via sdk_archive_merge, re-install into the SAME prefix,
# and verify the resulting libchronon3d_sdk_impl.a does NOT contain
# any of the gated .o files.  Hard-fail with `GHOST-FAIL: <objname>`
# if any leak through; emit `GHOST-OK` otherwise.
#
# Why: the manifest count gate (Fase 1 + Fase 5) verifies that the
# archive contains the EXPECTED objects, but does not prove the
# registry ENFORCES the feature toggles.  A builder that always picks
# up the full .o set regardless of CHRONON3D_BUILD_* flags would
# still pass the manifest gate.  This sweep proves the gate actually
# drops the gated compilation units when their toggle is OFF.
#
# After this OFF re-install, the downstream Fase-5 canary gate
# (Step 3.5) runs with the diagnostics canary correctly SKIPPED, but
# the unconditional subsystems (core/animations/scene/runtime/
# render_graph/software_backend/text_core) are still verified.
if [[ "${CHRONON3D_INSTALL_TEST_GHOST_SWEEP:-0}" == "1" ]]; then
    log "ghost sweep ON: switching SDK_BUILD to DIAG=OFF + CONTENT=OFF"
    cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
        -DCHRONON3D_BUILD_DIAGNOSTICS=OFF \
        -DCHRONON3D_BUILD_CONTENT=OFF 1>&2

    cmake --build "$SDK_BUILD" --target sdk_archive_merge -j8 1>&2

    cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2

    impl_archive_off="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
    if [[ -z "$impl_archive_off" ]]; then
        log "GHOST-FAIL: archive missing after OFF re-install"
        exit 1
    fi
    log "OFF archive: $impl_archive_off"

    ar_ghost_list="$GATE_TMP/ar_off.txt"
    ar t "$impl_archive_off" | sort > "$ar_ghost_list"
    ghost_fail_count=0
    for ghost in chronon3d_diagnostics.cpp.o \
                 chronon3d_backend_software_diagnostics.cpp.o \
                 chronon3d_content.cpp.o; do
        if grep -qF -- "$ghost" "$ar_ghost_list"; then
            log "GHOST-FAIL: $ghost"
            ghost_fail_count=$((ghost_fail_count + 1))
        fi
    done
    if (( ghost_fail_count > 0 )); then
        log "Ghost sweep: $ghost_fail_count feature-OFF object(s) leaked into OFF archive"
        log "              (registries must drop these .cpp.o when their CHRONON3D_BUILD_* flag is OFF)"
        exit 1
    fi
    log "GHOST-OK: feature-OFF archive is clean (no diagnostics/content ghost .o)"
fi

# ────────────────────────── Step 3: boundary pre-conditions ───────────
config_file="$(find "$SDK_PREFIX" -type f -name 'Chronon3DConfig.cmake' 2>/dev/null | head -1 || true)"
targets_file="$(find "$SDK_PREFIX" -type f -name 'Chronon3DTargets.cmake' 2>/dev/null | head -1 || true)"
impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
headers_dir="$SDK_PREFIX/include/chronon3d"

if [[ -z "$config_file" ]]; then
    log "FAIL: Chronon3DConfig.cmake not installed"
    exit 1
fi
if [[ -z "$targets_file" ]]; then
    log "FAIL: Chronon3DTargets.cmake not installed"
    exit 1
fi
if [[ -z "$impl_archive" ]]; then
    log "FAIL: libchronon3d_sdk_impl.a not installed"
    exit 1
fi
if [[ ! -d "$headers_dir" ]]; then
    log "FAIL: public headers not installed"
    exit 1
fi
header_count="$(find "$headers_dir" -maxdepth 6 -type f -name '*.hpp' 2>/dev/null | wc -l | tr -d ' ')"
if (( header_count == 0 )); then
    log "FAIL: no .hpp under $headers_dir"
    exit 1
fi
log "Boundary verified: config, targets, archive, $header_count headers"

# ────────────────────────── Step 3.5: ar t + nm canary gate ───────────
# Companion to cmake/Chronon3DCanarySymbols.cmake (Fase 4).  After file-level
# boundary pre-conditions (presence of config/targets/archive/headers), this
# step verifies the *archive content* is sane:
#   (a) ar t count >= 2  — marker TU plus >= 1 subsystem .o
#   (b) nm -C dump       — demangled symbol table (one-time per run)
#   (c) per canary entry in the catalog: substring match on the demangled
#       symbol.  Symbols behind conditional guards are skipped when the
#       guard is not satisfied (so text_off builds do not require text_core).
#
# HARD fail on any missing canary so the downstream consumer has zero
# chance to link against a torn archive.  All temp dirs are released by
# the cleanup trap registered at the top of the script (covers GATE_TMP
# on every exit path including bash's set -e firing mid-block).

# Pull option values from CMakeCache.txt; default to CMakeLists.txt
# defaults when absent.  Each var: read cache → fall back via parameter
# expansion `${var:=default}` only when empty.
sdk_cache="$SDK_BUILD/CMakeCache.txt"
cache_var() {
    # $1 = cache variable name; emit its value trimmed, or "" if absent.
    [[ -f "$sdk_cache" ]] || return 0
    grep -E "^${1}:" "$sdk_cache" 2>/dev/null \
        | head -1 | sed -E 's/^[^=]*=//' | tr -d ' \t\r\n' || true
}
text_on="$(cache_var CHRONON3D_ENABLE_TEXT)";    : "${text_on:=ON}"
diag_on="$(cache_var CHRONON3D_BUILD_DIAGNOSTICS)"; : "${diag_on:=OFF}"
blend2d_on="$(cache_var CHRONON3D_USE_BLEND2D)"; : "${blend2d_on:=ON}"
log "canary guards: text=$text_on diagnostics=$diag_on blend2d=$blend2d_on"

# (c) parse canary catalog into bash arrays.  Catalog is a CMake file; we
# extract each quoted "AREA|SYMBOL|GUARD|TARGET" entry via a robust grep
# that constrains each field to the catalog's allowed character set:
#   AREA   ∈ [a-z_]+
#   SYMBOL ∈ [a-zA-Z0-9_:]+   (C++ demangled: :: for namespaces)
#   GUARD  ∈ [a-zA-Z0-9_]+
#   TARGET ∈ [a-zA-Z0-9_]+
canary_file="$REPO_ROOT/cmake/Chronon3DCanarySymbols.cmake"
if [[ ! -f "$canary_file" ]]; then
    log "FAIL: canary catalog not found: $canary_file"
    exit 1
fi
canary_entries="$(grep -oE '"[a-z_]+\|[a-zA-Z0-9_:]+\|[a-zA-Z0-9_]+\|[a-zA-Z0-9_]+"' "$canary_file" 2>/dev/null || true)"
if [[ -z "$canary_entries" ]]; then
    log "FAIL: no canary entries parsed from $canary_file"
    exit 1
fi

# Load entries into bash arrays.  Each line is "AREA|SYMBOL|GUARD|TARGET".
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

if [[ -z "${TMP_ROOT:-}" ]]; then
    TMP_ROOT="${TMPDIR:-/tmp}"
fi
GATE_TMP="$(mktemp -d "$TMP_ROOT/chronon3d_install_gate.XXXXXX")"
ar_list="$GATE_TMP/ar.txt"
nm_dump="$GATE_TMP/nm.txt"

# (a) ar t output
if ! ar t "$impl_archive" | sort > "$ar_list" 2>/dev/null; then
    log "FAIL: ar t failed on $impl_archive"
    exit 1
fi
ar_count="$(wc -l < "$ar_list" | tr -d ' ')"
if (( ar_count < 2 )); then
    log "FAIL: archive contains only $ar_count .o files; need >= 2 (marker + >= 1 subsystem)"
    exit 1
fi
log "ar t: $impl_archive :: $ar_count .o files (>= 2)"

# (b) nm -C dump (one-time per run; subsequent greps are O(1) grep -F)
if ! nm -C "$impl_archive" > "$nm_dump" 2>/dev/null; then
    log "FAIL: nm -C failed on $impl_archive"
    exit 1
fi

# Walk each canary; honour GUARD; ok on substring hit; FAIL hard on miss.
checked=0
missing=0
skipped=0
fail_list=""
for i in "${!canary_areas[@]}"; do
    area="${canary_areas[$i]}"
    symbol="${canary_symbols[$i]}"
    guard="${canary_guards[$i]}"
    # target="${canary_targets[$i]}"  # currently advisory; logged for forensics.

    case "$guard" in
        always)
            skip_reason=""
            ;;
        CHRONON3D_ENABLE_TEXT)
            [[ "$text_on" == "ON" ]] || skip_reason="text_off (CHRONON3D_ENABLE_TEXT=$text_on)"
            ;;
        CHRONON3D_ENABLE_TEXT_AND_BLEND2D)
            if [[ "$text_on" != "ON" ]]; then
                skip_reason="text_off (CHRONON3D_ENABLE_TEXT=$text_on)"
            elif [[ "$blend2d_on" != "ON" ]]; then
                skip_reason="blend2d_off (CHRONON3D_USE_BLEND2D=$blend2d_on)"
            fi
            ;;
        CHRONON3D_BUILD_DIAGNOSTICS)
            [[ "$diag_on" == "ON" ]] || skip_reason="diag_off (CHRONON3D_BUILD_DIAGNOSTICS=$diag_on)"
            ;;
        *)
            log "WARN: unknown guard '$guard' for canary '$area'; treating as always"
            skip_reason=""
            ;;
    esac

    if [[ -n "$skip_reason" ]]; then
        log "SKIP: canary $area ($skip_reason)"
        skipped=$((skipped + 1))
        continue
    fi

    if grep -F -q -- "$symbol" "$nm_dump"; then
        log "OK: canary $symbol"
        checked=$((checked + 1))
    else
        log "FAIL: canary $area -> '$symbol' not present in $impl_archive"
        missing=$((missing + 1))
        fail_list="${fail_list}${fail_list:+, }$area"
    fi
done

if (( missing > 0 )); then
    log "FAIL: $missing canary symbol(s) missing from archive: $fail_list"
    log "      (rebuild with the matching CHRONON3D_ENABLE_* option enabled or fix the catalog)"
    exit 1
fi
log "Canary gate: $checked present, $skipped skipped, 0 missing"

# ────────────────────────── Step 4: consumer ──────────────────────────
log "Configuring consumer (tests/install_consumer/)"
CONS_PREFIX_PATH="$SDK_PREFIX"
if [[ -n "${VCPKG_INSTALLED_DIR:-}" ]]; then
    local_vcpkg_triplet="${VCPKG_TARGET_TRIPLET:-x64-linux}"
    CONS_PREFIX_PATH="$CONS_PREFIX_PATH;${VCPKG_INSTALLED_DIR}/${local_vcpkg_triplet}"
fi
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    CONS_PREFIX_PATH="$CONS_PREFIX_PATH;${CMAKE_PREFIX_PATH}"
fi

CMAKE_ARGS=(
    "-S" "$REPO_ROOT/tests/install_consumer"
    "-B" "$CONS_BUILD"
    "-DCMAKE_PREFIX_PATH=$CONS_PREFIX_PATH"
    "-DCMAKE_BUILD_TYPE=Release"
)
VCPKG_TC="$REPO_ROOT/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
if [[ -f "$VCPKG_TC" ]]; then
    CMAKE_ARGS+=( "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_TC" )
elif [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=( "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" )
fi
cmake "${CMAKE_ARGS[@]}" 1>&2

log "Building consumer"
cmake --build "$CONS_BUILD" --target check_install 1>&2

consumer_bin="$CONS_BUILD/check_install"
if [[ ! -x "$consumer_bin" ]]; then
    log "FAIL: consumer binary not found"
    exit 1
fi

log "Running consumer"
consumer_output="$("$consumer_bin")"
if [[ "$consumer_output" != *"[BOUNDARY-OK]"* ]]; then
    log "FAIL: consumer missing [BOUNDARY-OK] marker"
    printf "%s\n" "$consumer_output" >&2
    exit 1
fi
log "Consumer: $consumer_output"

# ────────────────────────── Step 5: verify PNG output ─────────────────
output_png="$CONS_BUILD/sdk_consumer_output.png"
if [[ ! -f "$output_png" ]]; then
    log "FAIL: output PNG not found: $output_png"
    exit 1
fi
png_size="$(stat -c%s "$output_png" 2>/dev/null || echo 0)"
if (( png_size == 0 )); then
    log "FAIL: output PNG is empty"
    exit 1
fi
log "PNG verified: $output_png ($png_size bytes)"

# ────────────────────────── Step 6: summary ───────────────────────────
printf '{"test":"install_consumer_ci","status":"passed","preset":"%s","png_bytes":%d}\n' \
    "$PRESET" "$png_size"
