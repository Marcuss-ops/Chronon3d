#!/usr/bin/env bash
# tools/sdk/common.sh — shared callable library for the install-consume
# boundary CI test pipeline.
#
# Sourced (NOT executed) by:
#   * tools/install_consumer_test.sh       (orchestrator)
#   * tools/sdk/check_archive_canaries.sh  (Step 3.5)
#   * tools/sdk/check_feature_ghosts.sh    (Step 2.5)
#   * tools/sdk/run_external_consumer.sh   (Steps 4-5)
#
# Conventions enforced:
#   * strict mode (`set -euo pipefail`) re-applied at top; safe to source
#     twice — the include guard below short-circuits the second source.
#   * log()   → stderr with `[install_consumer_test]` prefix.
#   * fail()  → log + exit 1 (one exit code shared by all phases).
#   * REPO_ROOT resolution from caller's BASH_SOURCE so bash subshell
#     invocations work identically.
#   * cache_var() reads CMakeCache.txt with safe empty fallback (caller
#     can supply default via `${cache_var_name:=default}` parameter exp.).
#   * mktemp_dir() wraps mktemp with the project's TMPDIR policy.
#
# Phase scripts MUST source this file via:
#     source "$(dirname "$0")/common.sh"
# …relative to the phase script's path (works under `bash tools/sdk/x.sh`
# invocations from any cwd).

# ── Include guard (idempotent re-sourcing safety) ────────────────────
if [[ -n "${_CHRONON3D_COMMON_SH_LOADED:-}" ]]; then
    return 0
fi
_CHRONON3D_COMMON_SH_LOADED=1

set -euo pipefail

# ── Logging ───────────────────────────────────────────────────────────
log()  { printf "[install_consumer_test] %s\n" "$*" >&2; }
fail() { log "FAIL: $*"; exit 1; }

# ── CMake version guard ───────────────────────────────────────────────
require_cmake_3_27() {
    command -v cmake >/dev/null || fail "cmake not on PATH"
    local v; v="$(cmake --version | head -1 | awk '{print $3}')"
    local major minor
    major="$(echo "$v" | cut -d. -f1)"
    minor="$(echo "$v" | cut -d. -f2)"
    if (( major < 3 || (major == 3 && minor < 27) )); then
        fail "cmake >=3.27 required (have $v)"
    fi
    echo "$v"
}

# ── REPO_ROOT resolution (works under bash subshell / direct exec) ────
# Callers are typically `tools/install_consumer_test.sh` (depth 1) or
# `tools/sdk/<phase>.sh` (depth 2).  Fallback returns the resolved dir
# verbatim when the call stack doesn't match either pattern (test
# invocations can `source` this directly).
resolve_repo_root() {
    local src="${BASH_SOURCE[1]:-$0}"
    local dir; dir="$(cd "$(dirname "$src")" && pwd)"
    case "$dir" in
        */tools)       (cd "$dir/.." && pwd);;
        */tools/sdk)   (cd "$dir/../.." && pwd);;
        *)             echo "$dir";;
    esac
}

# ── Temp dir factory (project TMPDIR policy) ──────────────────────────
mktemp_dir() {
    local prefix="${1:-chronon3d_install}"
    local root="${TMPDIR:-/tmp}"
    mktemp -d "$root/${prefix}.XXXXXX"
}

# ── CMakeCache.txt reader ─────────────────────────────────────────────
# Reads the cached value of a CMake variable from
# `$SDK_BUILD/CMakeCache.txt`.  Returns "" (empty) if the cache is
# missing OR the variable is unset.  Caller is expected to apply
# sensible defaults via parameter expansion (`${var:=default}`).
#
# Usage:
#     text_on="$(cache_var CHRONON3D_ENABLE_TEXT)"; : "${text_on:=ON}"
cache_var() {
    local cache_file="${SDK_BUILD}/CMakeCache.txt"
    [[ -f "$cache_file" ]] || return 0
    grep -E "^${1}:" "$cache_file" 2>/dev/null \
        | head -1 | sed -E 's/^[^=]*=//' | tr -d ' \t\r\n' || true
}

# ── Cleanup-trap registrar ────────────────────────────────────────────
# Registers an EXIT trap that removes each named temp dir (idempotent
# against empty / unset entries).  Reusable across orchestrator +
# phase scripts; directories accumulate across calls (global array).
# The trap preserves the original exit code so failures propagate
# correctly through the cleanup.
_CLEANUP_DIRS=()
cleanup_register() {
    _CLEANUP_DIRS+=("$@")
    trap '
        rc=$?
        for d in "${_CLEANUP_DIRS[@]}"; do
            [[ -n "$d" && -d "$d" ]] && rm -rf "$d" 2>/dev/null || true
        done
        exit "$rc"
    ' EXIT
}

# ── chmod convention reminder (informational only) ────────────────────
# The 3 phase scripts under tools/sdk/ declare `#!/usr/bin/env bash`
# and are intended to be invoked as `bash tools/sdk/<phase>.sh` from
# the orchestrator.  Local-engine convenience: `chmod +x` after
# checkout so `./tools/sdk/<phase>.sh` also works.
:
