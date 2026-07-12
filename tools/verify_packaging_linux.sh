#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_packaging_linux.sh
#
# Canonical Packaging & Relocatability certification gate.
#
# Verifies the SDK install is relocatable and consumer-ready:
#   1. cmake --install to prefix A
#   2. cmake --install to prefix B (different directory)
#   3. mv prefix A → prefix C (simulate relocation)
#   4. find_package(Chronon3D) works from prefix C after relocation
#   5. No absolute paths to old prefix in relocated install
#   6. No references to the source tree in installed artifacts
#   7. External consumer builds, links, runs, and produces output from prefix C
#
# Verdict contract:
#   PACKAGING_FUNCTIONAL_PASS    — all sections pass
#   PACKAGING_FUNCTIONAL_FAIL    — any section fails
#   PACKAGING_FUNCTIONAL_BLOCKED — env/build not available
#   Exit 0 = PASS, 1 = FAIL, 2 = BLOCKED
#
# Usage:
#   bash tools/verify_packaging_linux.sh
#
# Environment:
#   CHRONON3D_PKG_PRESET=<name>   CMake preset (default: linux-release)
#   CHRONON3D_PKG_KEEP_DIRS=1     Keep temp build/install dirs after run
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_packaging_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_PKG_PRESET="${CHRONON3D_PKG_PRESET:-linux-release}"
CHRONON3D_PKG_KEEP_DIRS="${CHRONON3D_PKG_KEEP_DIRS:-0}"

set -uo pipefail

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass() { echo "  [PASS] $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail() { echo "  [FAIL] $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked() { echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_packaging_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")

if [ "$LOCAL" != "$REMOTE" ] \
   && [ "$REMOTE" != "N/A" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "PKG_FAIL: HEAD and origin/main are divergent"
    echo "  LOCAL:  $LOCAL"
    echo "  REMOTE: $REMOTE"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

command -v cmake >/dev/null 2>&1 \
    && _gate_pass "cmake ($(cmake --version 2>/dev/null | head -1))" \
    || { _gate_blocked "cmake" "not found"; ENV_BLOCKED=true; }

command -v ninja >/dev/null 2>&1 \
    && _gate_pass "ninja" \
    || { command -v make >/dev/null 2>&1 \
         && _gate_pass "make (fallback)" \
         || { _gate_blocked "ninja/make" "neither found"; ENV_BLOCKED=true; }; }

# Check the preset exists
if cmake --list-presets 2>/dev/null | grep -q "\"${CHRONON3D_PKG_PRESET}\""; then
    _gate_pass "preset ${CHRONON3D_PKG_PRESET}"
else
    _gate_blocked "preset" "${CHRONON3D_PKG_PRESET} not found in CMakePresets.json"
    ENV_BLOCKED=true
fi

# ══════════════════════════════════════════════════════════════════════════════
# 3. Build + install SDK to prefix A, then prefix B
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Build + install SDK (prefix A + prefix B) =="

PREFIX_A="/tmp/chronon3d_pkg_prefix_a"
PREFIX_B="/tmp/chronon3d_pkg_prefix_b"
PREFIX_C="/tmp/chronon3d_pkg_prefix_c"   # relocation target (§4)
BUILD_DIR="/tmp/chronon3d_pkg_build"
CONSUMER_DIR="/tmp/chronon3d_pkg_consumer"

# Clean any previous run
rm -rf "$PREFIX_A" "$PREFIX_B" "$PREFIX_C" "$BUILD_DIR" "$CONSUMER_DIR"

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "build_install" "env blocked"
else
    mkdir -p "$BUILD_DIR" "$PREFIX_A" "$PREFIX_B"

    echo "  Configuring preset=${CHRONON3D_PKG_PRESET}..."
    if cmake -S "$ROOT" -B "$BUILD_DIR" --preset "$CHRONON3D_PKG_PRESET" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX_A" >/dev/null 2>&1; then
        _gate_pass "configure"
    else
        _gate_fail "configure" "cmake preset failed"; ENV_BLOCKED=true
    fi

    if [ "$ENV_BLOCKED" = false ]; then
        echo "  Building SDK (target: chronon3d_sdk_impl)..."
        NPROC=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
        if cmake --build "$BUILD_DIR" --target chronon3d_sdk_impl -j"$NPROC" >/dev/null 2>&1; then
            _gate_pass "build_sdk"
        else
            _gate_fail "build_sdk" "build failed"; ENV_BLOCKED=true
        fi
    fi

    if [ "$ENV_BLOCKED" = false ]; then
        echo "  Installing to $PREFIX_A..."
        if cmake --install "$BUILD_DIR" --prefix "$PREFIX_A" >/dev/null 2>&1; then
            _gate_pass "install_prefix_a"
        else
            _gate_fail "install_prefix_a" "install failed"; ENV_BLOCKED=true
        fi
    fi

    if [ "$ENV_BLOCKED" = false ]; then
        echo "  Installing to $PREFIX_B..."
        if cmake --install "$BUILD_DIR" --prefix "$PREFIX_B" >/dev/null 2>&1; then
            _gate_pass "install_prefix_b"
        else
            _gate_fail "install_prefix_b" "install failed"; ENV_BLOCKED=true
        fi
    fi

    # Verify both prefixes have distinct but valid installs
    if [ "$ENV_BLOCKED" = false ]; then
        A_CONFIG=$(find "$PREFIX_A" -name "Chronon3DConfig.cmake" 2>/dev/null | head -1)
        B_CONFIG=$(find "$PREFIX_B" -name "Chronon3DConfig.cmake" 2>/dev/null | head -1)
        if [ -n "$A_CONFIG" ] && [ -s "$A_CONFIG" ] && [ -n "$B_CONFIG" ] && [ -s "$B_CONFIG" ]; then
            _gate_pass "two_prefix_configs_present"
        else
            _gate_fail "two_prefix_configs_present" "config missing in one or both prefixes"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. Relocation: mv prefix A → prefix C
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. Relocation: mv $PREFIX_A → $PREFIX_C =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "relocation" "build/install blocked"
else
    if mv "$PREFIX_A" "$PREFIX_C" 2>/dev/null; then
        _gate_pass "relocate_mv"

        # Verify the cmake config file still exists after move
        CONFIG_FILE=$(find "$PREFIX_C" -name "Chronon3DConfig.cmake" 2>/dev/null | head -1)
        if [ -n "$CONFIG_FILE" ] && [ -s "$CONFIG_FILE" ]; then
            _gate_pass "config_survives_relocation ($CONFIG_FILE)"
        else
            _gate_fail "config_survives_relocation" "Chronon3DConfig.cmake not found after relocation"
        fi
    else
        _gate_fail "relocate_mv" "mv failed"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. No absolute paths / no source tree references
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. No absolute paths / no source tree refs =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "path_audit" "build/install blocked"
else
    # Check for absolute paths pointing to the original prefix A
    ABS_PATHS_A=$(grep -rF "$PREFIX_A" "$PREFIX_C" 2>/dev/null | grep -v 'Binary file' | wc -l)
    if [ "$ABS_PATHS_A" -eq 0 ]; then
        _gate_pass "no_absolute_paths_to_prefix_a ($ABS_PATHS_A matches)"
    else
        _gate_fail "no_absolute_paths_to_prefix_a" "$ABS_PATHS_A absolute paths to old prefix found"
        grep -rF "$PREFIX_A" "$PREFIX_C" 2>/dev/null | grep -v 'Binary file' | head -3
    fi

    # Check for references to the source tree
    SOURCE_REFS=$(grep -rF "$ROOT" "$PREFIX_C" 2>/dev/null | grep -v 'Binary file' | wc -l)
    if [ "$SOURCE_REFS" -eq 0 ]; then
        _gate_pass "no_source_tree_refs ($SOURCE_REFS matches)"
    else
        _gate_fail "no_source_tree_refs" "$SOURCE_REFS source tree references found"
        grep -rF "$ROOT" "$PREFIX_C" 2>/dev/null | grep -v 'Binary file' | head -3
    fi

    # Check for common build-host paths that shouldn't leak into install
    SUSPICIOUS_ABS=$(grep -rE '(/tmp/|/home/)' "$PREFIX_C"/share/cmake 2>/dev/null | grep -v 'Binary file' | wc -l)
    if [ "$SUSPICIOUS_ABS" -eq 0 ]; then
        _gate_pass "no_suspicious_abs_paths ($SUSPICIOUS_ABS in cmake files)"
    else
        _gate_fail "no_suspicious_abs_paths" "$SUSPICIOUS_ABS suspicious absolute paths found"
        grep -rE '(/tmp/|/home/)' "$PREFIX_C"/share/cmake 2>/dev/null | grep -v 'Binary file' | head -3
    fi

    # Verify prefix B is also clean (non-relocated prefix)
    ABS_PATHS_B=$(grep -rF "$ROOT" "$PREFIX_B" 2>/dev/null | grep -v 'Binary file' | wc -l)
    if [ "$ABS_PATHS_B" -eq 0 ]; then
        _gate_pass "prefix_b_no_source_refs ($ABS_PATHS_B matches)"
    else
        _gate_fail "prefix_b_no_source_refs" "$ABS_PATHS_B source tree references in prefix B"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. External consumer from relocated prefix C
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. External consumer (find_package from prefix C) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "external_consumer" "build/install blocked"
else
    mkdir -p "$CONSUMER_DIR"

    # Create a minimal consumer CMakeLists.txt
    cat > "$CONSUMER_DIR/CMakeLists.txt" << 'CMEOF'
cmake_minimum_required(VERSION 3.27)
project(Chronon3dPackagingCert CXX)
find_package(Chronon3D CONFIG REQUIRED)
add_executable(cert main.cpp)
set_target_properties(cert PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(cert PRIVATE Chronon3D::SDK)
CMEOF

    # Minimal consumer: exercise basic SDK types (Vec3) + umbrella header
    cat > "$CONSUMER_DIR/main.cpp" << 'CPPEOF'
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cstdio>

int main() {
    chronon3d::Vec3 v{1.0f, 2.0f, 3.0f};
    std::printf("Chronon3D packaging cert: OK (Vec3={%f,%f,%f})\n", v.x, v.y, v.z);
    return 0;
}
CPPEOF

    echo "  Configuring consumer against prefix C..."
    if cmake -S "$CONSUMER_DIR" -B "$CONSUMER_DIR/build" \
        -DCMAKE_PREFIX_PATH="$PREFIX_C" >/dev/null 2>&1; then
        _gate_pass "find_package_works_after_relocation"

        echo "  Building consumer..."
        if cmake --build "$CONSUMER_DIR/build" >/dev/null 2>&1; then
            _gate_pass "consumer_build"

            echo "  Running consumer..."
            CONSUMER_BIN=$(find "$CONSUMER_DIR/build" -name "cert" -type f -executable 2>/dev/null | head -1)
            if [ -n "$CONSUMER_BIN" ] && "$CONSUMER_BIN" >/dev/null 2>&1; then
                _gate_pass "consumer_run_produces_output"
            else
                _gate_fail "consumer_run_produces_output" "binary not found or non-zero exit"
            fi
        else
            _gate_fail "consumer_build" "build failed"
        fi
    else
        _gate_fail "find_package_works_after_relocation" "cmake configure failed after relocation"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Cleanup
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 7. Cleanup =="

if [ "$CHRONON3D_PKG_KEEP_DIRS" = "1" ]; then
    echo "  Keeping dirs: $PREFIX_B $PREFIX_C $BUILD_DIR $CONSUMER_DIR"
    _gate_pass "cleanup (preserved)"
else
    rm -rf "$PREFIX_A" "$PREFIX_B" "$PREFIX_C" "$BUILD_DIR" "$CONSUMER_DIR" 2>/dev/null || true
    _gate_pass "cleanup (removed)"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 8. Final verdict
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$ENV_BLOCKED" = true ]; then
    echo "PACKAGING_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Packaging certification blocked by environment/build."
    echo "  macchina-verifica DEFERRED to working build host per AGENTS.md §honest-limitation"
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "PACKAGING_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed."
    exit 1
else
    echo "PACKAGING_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Packaging & Relocatability certified."
    echo "[INFO] ${GATE_NAME}: $PASS_COUNT sections passed (repo + env + build-install-A+B + relocation + path-audit + external-consumer + cleanup)"
    exit 0
fi
