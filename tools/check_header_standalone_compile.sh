#!/usr/bin/env bash
# tools/check_header_standalone_compile.sh
# ─────────────────────────────────────────────────────────────────────
# Fase A1 — Standalone header compile gate.
#
# For each public header listed in the canonical manifest
# (cmake/Chronon3DPublicHeaders.cmake), generates a minimal translation
# unit that includes ONLY that header and compiles it with -fsyntax-only
# against the SDK include paths plus vcpkg dependencies.  Detects:
#   - Missing transitive includes
#   - Broken internal paths (e.g. stale symlink targets removed)
#   - Headers that depend on types not available through their own
#     include chain
#
# Env vars:
#   SDK_PREFIX         Install prefix (enables INSTALL mode when set)
#   REPO_ROOT          Project root (default: git rev-parse)
#   VCPKG_INSTALLED_DIR Vcpkg installed packages (auto-detected)
#   CXX                C++ compiler (default: auto-detect)
#
# Exit codes:
#   0 = all headers compiled successfully
#   1 = at least one header failed
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

# ── Compiler detection ───────────────────────────────────────────────
CXX="${CXX:-c++}"
if ! command -v "$CXX" &>/dev/null; then
    for candidate in g++ clang++ c++; do
        if command -v "$candidate" &>/dev/null; then
            CXX="$candidate"
            break
        fi
    done
fi

if ! command -v "$CXX" &>/dev/null; then
    echo "FATAL: no C++ compiler found (tried: c++, g++, clang++)" >&2
    exit 2
fi

CXX_VERSION=$("$CXX" --version 2>&1 | head -1)
echo "=== Standalone header compile gate ==="
echo "compiler    : $CXX ($CXX_VERSION)"
echo "repo root   : $REPO_ROOT"

# ── Determine include paths ──────────────────────────────────────────
SCRATCH_DIR="$(mktemp -d "${TMPDIR:-/tmp}/chronon3d_header_compile.XXXXXX")"
trap 'rm -rf "$SCRATCH_DIR"' EXIT

if [[ -n "${SDK_PREFIX:-}" ]] && [[ -d "$SDK_PREFIX/include" ]]; then
    MODE="INSTALL"
    INCLUDE_DIR="$SDK_PREFIX/include"
    echo "mode        : INSTALL ($INCLUDE_DIR)"
else
    MODE="SOURCE"
    INCLUDE_DIR="$REPO_ROOT/include"
    echo "mode        : SOURCE ($INCLUDE_DIR)"
fi

# ── Auto-detect vcpkg include paths ──────────────────────────────────
VCPKG_DIR="${VCPKG_INSTALLED_DIR:-}"
if [[ -z "$VCPKG_DIR" ]]; then
    # Try common locations
    for candidate in \
        "$REPO_ROOT/vcpkg_installed/x64-linux" \
        "$REPO_ROOT/vcpkg_installed/arm64-linux" \
        "$REPO_ROOT/build/vcpkg_installed/x64-linux"; do
        if [[ -d "$candidate/include" ]]; then
            VCPKG_DIR="$candidate"
            break
        fi
    done
fi

VCPKG_INCLUDES=()
if [[ -n "$VCPKG_DIR" ]] && [[ -d "$VCPKG_DIR/include" ]]; then
    echo "vcpkg       : $VCPKG_DIR"
    VCPKG_INCLUDES+=(-I"$VCPKG_DIR/include")
    for sub in "$VCPKG_DIR/include"/*/; do
        [[ -d "$sub" ]] && VCPKG_INCLUDES+=(-I"${sub%/}")
    done
else
    echo "vcpkg       : NOT FOUND (some failures expected for external deps)"
fi

# ── Parse the public header manifest ─────────────────────────────────
MANIFEST="$REPO_ROOT/cmake/Chronon3DPublicHeaders.cmake"
if [[ ! -f "$MANIFEST" ]]; then
    echo "FATAL: public header manifest not found at $MANIFEST" >&2
    exit 2
fi

# Extract header paths from the manifest.  Each entry is of the form:
#   "${CMAKE_SOURCE_DIR}/include/chronon3d/<relative-path>"
# We extract the <relative-path> portion (e.g. "chronon3d.hpp" or
# "sdk/render_engine.hpp") so we can #include it directly.
# Skip .inl files — these are inline implementation files never meant
# to be included standalone.
mapfile -t HEADER_REL_PATHS < <(
    grep -E '^[[:space:]]*"\$\{CMAKE_SOURCE_DIR\}/include/chronon3d/' "$MANIFEST" \
    | sed -E 's|.*include/chronon3d/([^"]+)".*|\1|' \
    | grep -v '\.inl$' \
    | sort
)

if [[ ${#HEADER_REL_PATHS[@]} -eq 0 ]]; then
    echo "FATAL: no headers found in manifest $MANIFEST" >&2
    exit 2
fi

echo "header count: ${#HEADER_REL_PATHS[@]} (.inl files excluded)"
echo ""

# ── Compile flags ────────────────────────────────────────────────────
# -fsyntax-only: parse + semantic analysis, no codegen (fast)
# -std=c++20   : project standard
# -c           : compile only, no link
# -Wno-unknown-pragmas: tolerate vendor pragmas
# -Wno-attributes      : tolerate vendor attributes
COMMON_FLAGS=(-std=c++20 -fsyntax-only -c -Wno-unknown-pragmas -Wno-attributes)

INCLUDE_FLAGS=(-I"$INCLUDE_DIR" "${VCPKG_INCLUDES[@]}")

# ── Compile each header standalone ───────────────────────────────────
PASSED=0
FAILED=0
FAILED_LIST=()

echo "--- Compiling each header standalone ---"
echo ""

for rel_path in "${HEADER_REL_PATHS[@]}"; do
    # Create minimal translation unit
    tu_file="$SCRATCH_DIR/test_$(echo "$rel_path" | tr '/' '_' | tr '.' '_').cpp"
    echo "#include <chronon3d/$rel_path>" > "$tu_file"

    # Compile using bash arrays for safe argument passing
    if "$CXX" "${COMMON_FLAGS[@]}" "${INCLUDE_FLAGS[@]}" "$tu_file" -o /dev/null 2>"$SCRATCH_DIR/compile_err.txt"; then
        echo -n "  PASS  "
        PASSED=$((PASSED + 1))
    else
        echo -n "  FAIL  "
        FAILED=$((FAILED + 1))
        FAILED_LIST+=("$rel_path")
    fi
    printf "%-65s" "chronon3d/$rel_path"

    # Show first and last error lines for failures (last line often has the
    # actual error in template-heavy code with deep include chains)
    if [[ -s "$SCRATCH_DIR/compile_err.txt" ]]; then
        first_err=$(head -1 "$SCRATCH_DIR/compile_err.txt")
        last_err=$(tail -1 "$SCRATCH_DIR/compile_err.txt")
        if [[ ${#first_err} -gt 120 ]]; then
            first_err="${first_err:0:117}..."
        fi
        if [[ ${#last_err} -gt 120 ]]; then
            last_err="${last_err:0:117}..."
        fi
        echo "  first: $first_err"
        if [[ "$first_err" != "$last_err" ]]; then
            echo "         last : $last_err"
        fi
    else
        echo ""
    fi
done

echo ""
echo "=== Summary ==="
echo "total  : ${#HEADER_REL_PATHS[@]}"
echo "passed : $PASSED"
echo "failed : $FAILED"

if [[ $FAILED -gt 0 ]]; then
    echo ""
    echo "Failed headers:"
    for h in "${FAILED_LIST[@]}"; do
        echo "  - chronon3d/$h"
    done
    echo ""
    echo "=== Standalone header compile gate FAILED ==="
    exit 1
fi

echo "=== All headers compile standalone! ==="
exit 0
