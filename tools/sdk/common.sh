#!/usr/bin/env bash
# tools/sdk/common.sh — Shared helpers for tools/install_consumer_test.sh
#
# PURPOSE
#   Single source of truth for the bash plumbing consumed by each phase
#   script in tools/sdk/:
#     • tools/sdk/check_archive_members.sh
#     • tools/sdk/check_archive_canaries.sh
#     • tools/sdk/check_feature_ghosts.sh
#     • tools/sdk/run_external_consumer.sh
#   Plus `tools/install_consumer_test.sh` (the thin orchestrator).
#
# CONTRACT
#   • Each phase script MUST `source "$(dirname "$0")/common.sh"` as its
#     first executable line (after `#! …` and the header).
#   • Phase scripts communicate with the orchestrator via SHELL VARIABLES
#     (set with `export VAR=…`), not via stdin/stdout.  The orchestrator
#     exports, the phase script reads, no temporary files.  Variables:
#       SDK_BUILD    — mktemp'd build dir for the SDK
#       SDK_PREFIX   — mktemp'd install prefix
#       CONS_BUILD   — mktemp'd build dir for tests/install_consumer
#       GATE_TMP     — mktemp'd working dir for ar t / nm -C artifacts
#       PRESET       — cmake preset (default: linux-ci)
#       REPO_ROOT    — repo root (resolved from common.sh's dirname)
#       TMP_ROOT     — base for mktemp (default: /tmp or $TMPDIR)
#   • `cleanup_trap` is registered automatically at source time and
#     removes every temp var that's non-empty on EXIT.  Phases do NOT
#     install their own cleanup traps.
#   • Helpers exposed:
#       log <msg>                 — log to stderr (CI-friendly).
#       require_cmake_3_25        — exit 1 if cmake < 3.25.
#       make_temp_dirs            — produce SDK_BUILD/SDK_PREFIX/CONS_BUILD.
#       register_cleanup_trap     — registers trap for SDK_BUILD/etc.
#       load_cache_var <var>      — read a CMakeCache.txt var (stdout).
#       parse_canary_catalog      — populate canary_areas/symbols/guards arrays.
#       emit_summary_json         — single-line JSON for CI log scraping.
#   • `set -euo pipefail` is enforced on entry.  Phases inherit strict mode.
# ==============================================================================

set -euo pipefail

# ── Defaults and discovery ──────────────────────────────────────────
INSTALL_TEST_PRESET="${CHRONON3D_INSTALL_TEST_PRESET:-linux-ci}"
: "${PRESET:=$INSTALL_TEST_PRESET}"
# REPO_ROOT is two levels up from this file: tools/sdk/common.sh →
#   tools/ → repo root.
# Allow override for sandbox paths (CI sets REPO_ROOT explicitly).
if [[ -z "${REPO_ROOT:-}" ]]; then
    REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fi
TMP_ROOT="${TMPDIR:-/tmp}"

# ── Logging ────────────────────────────────────────────────────────
# Per-line stderr write so CTest / `set -x` follows the script faithfully.
log() { printf '[install_consumer_test] %s\n' "$*" >&2; }

# ── Toolchain gate ─────────────────────────────────────────────────
require_cmake_3_25() {
    command -v cmake >/dev/null || { log "FAIL: cmake not on PATH"; exit 1; }
    local v
    v="$(cmake --version | head -1 | awk '{print $3}')"
    local major minor
    major="$(echo "$v" | cut -d. -f1)"
    minor="$(echo "$v" | cut -d. -f2)"
    if (( major < 3 || (major == 3 && minor < 25) )); then
        log "FAIL: cmake >=3.25 required (have $v)"
        exit 1
    fi
    log "cmake $v at $(command -v cmake)"
}

# ── Temp-dir lifecycle ────────────────────────────────────────────
# Single source of truth for the three persistent temp dirs (SDK build,
# SDK install prefix, consumer build).  Each phase that needs a gate
# tmp calls `make_gate_tmp` separately.
make_temp_dirs() {
    [[ -z "${SDK_BUILD:-}"  ]] && SDK_BUILD="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_sdk_build.XXXXXX")"
    [[ -z "${SDK_PREFIX:-}" ]] && SDK_PREFIX="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_prefix.XXXXXX")"
    [[ -z "${CONS_BUILD:-}" ]] && CONS_BUILD="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_build.XXXXXX")"
    export SDK_BUILD SDK_PREFIX CONS_BUILD
    log "temp sdk build : $SDK_BUILD"
    log "temp install   : $SDK_PREFIX"
    log "temp consumer  : $CONS_BUILD"
}

make_gate_tmp() {
    [[ -z "${GATE_TMP:-}" ]] && GATE_TMP="$(mktemp -d "$TMP_ROOT/chronon3d_install_gate.XXXXXX")"
    export GATE_TMP
}

# Register the EXIT trap once at source time.  Removes any non-empty
# of {SDK_BUILD, SDK_PREFIX, CONS_BUILD, GATE_TMP} on exit, regardless
# of which phase script created them.  Re-sourcing this file is a
# no-op (the trap is idempotent via function declared below).
register_cleanup_trap() {
    cleanup() {
        local rc=$?
        [[ -n "${SDK_BUILD:-}" ]]  && rm -rf "$SDK_BUILD"
        [[ -n "${SDK_PREFIX:-}" ]] && rm -rf "$SDK_PREFIX"
        [[ -n "${CONS_BUILD:-}" ]] && rm -rf "$CONS_BUILD"
        [[ -n "${GATE_TMP:-}"   ]] && rm -rf "$GATE_TMP"
        exit "$rc"
    }
    trap cleanup EXIT
}
register_cleanup_trap

# ── CMakeCache.txt reader ────────────────────────────────────────
# Pull a single CMakeCache.txt value (trimmed) from $SDK_BUILD, or
# empty string if the var is not set in cache.  Each call is a single
# grep+head+sed; cheap.
load_cache_var() {
    local var="$1"
    local cache="${SDK_BUILD}/CMakeCache.txt"
    [[ -f "$cache" ]] || return 0
    grep -E "^${var}:" "$cache" 2>/dev/null \
        | head -1 | sed -E 's/^[^=]*=//' | tr -d ' \t\r\n' || true
}

# ── Canary catalog parser ────────────────────────────────────────
# Extract `AREA|SYMBOL|GUARD|TARGET` tuples from the CMake-format
# catalog.  After this call, canary_areas[i] / canary_symbols[i] /
# canary_guards[i] / canary_targets[i] are populated (parallel arrays).
parse_canary_catalog() {
    local catalog="${REPO_ROOT}/cmake/Chronon3DCanarySymbols.cmake"
    if [[ ! -f "$catalog" ]]; then
        log "FAIL: canary catalog not found: $catalog"
        exit 1
    fi
    local entries
    entries="$(grep -oE '"[a-z_]+\|[a-zA-Z0-9_:]+\|[a-zA-Z0-9_]+\|[a-zA-Z0-9_]+"' "$catalog" 2>/dev/null || true)"
    if [[ -z "$entries" ]]; then
        log "FAIL: no canary entries parsed from $catalog"
        exit 1
    fi
    canary_areas=()
    canary_symbols=()
    canary_guards=()
    canary_targets=()
    while IFS= read -r entry; do
        local body="${entry#\"}"
        body="${body%\"}"
        IFS='|' read -r area symbol guard target <<<"$body"
        canary_areas+=("$area")
        canary_symbols+=("$symbol")
        canary_guards+=("$guard")
        canary_targets+=("$target")
    done <<<"$entries"
}

# ── Final JSON summary line ──────────────────────────────────────
emit_summary_json() {
    local png_size="${1:-0}"
    printf '{"test":"install_consumer_ci","status":"passed","preset":"%s","png_bytes":%d}\n' \
        "$PRESET" "$png_size"
}
