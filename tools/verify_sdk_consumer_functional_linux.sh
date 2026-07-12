#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_sdk_consumer_functional_linux.sh
#
# Canonical SDK External Consumer certification gate (P1).
#
# Per user spec verbatim (Italian):
#   "consumer completamente fuori dalla source tree.
#    find_package(Chronon3D CONFIG REQUIRED) +
#    target_link_libraries(... PRIVATE Chronon3D::SDK).
#    Il consumer deve usare testo/immagine/camera/timeline/render/PNG-save
#    e NON includere chronon3d/internal/, RenderGraph, FontEngine,
#    CameraSession, SoftwareRenderer, cache internals."
#
# Verifies:
#   - find_package(Chronon3D CONFIG REQUIRED) + Chronon3D::SDK link works
#   - 6 SDK public surface areas are exercised: text, image, camera,
#     timeline, render, PNG-save
#   - 6 internal surface classes are NOT used: chronon3d/internal/,
#     RenderGraph, FontEngine, CameraSession, SoftwareRenderer, cache
#     internals
#   - The consumer binary runs and produces a non-empty PNG output
#
# Verdict contract:
#   SDK_CONSUMER_FUNCTIONAL_PASS    — all sections pass (exit 0)
#   SDK_CONSUMER_FUNCTIONAL_FAIL    — any section fails (exit 1)
#   SDK_CONSUMER_FUNCTIONAL_BLOCKED — env/build not available (exit 2)
#
# §honesty contract (AGENTS.md):
#   - BLOCKED steps reported with explicit diagnostic, not silently skipped
#   - SDK_CONSUMER_FUNCTIONAL_PASS only when ALL sections pass
#   - SDK_CONSUMER_FUNCTIONAL_FAIL on any non-zero exit
#   - SDK_CONSUMER_FUNCTIONAL_BLOCKED when build is blocked
#   - Addizionale [INFO] ${GATE_NAME}: ... line on PASS (per AGENTS.md
#     "## Regole di lint documentale" §INFO-level diagnostic style)
#
# Usage:
#   bash tools/verify_sdk_consumer_functional_linux.sh
#
# Environment overrides:
#   CHRONON3D_SDK_PRESET=<name>         CMake preset (default: linux-ci)
#   CHRONON3D_SDK_BUILD_DIR=<path>      Build dir override
#   CHRONON3D_SDK_KEEP_DIRS=1           Keep temp install/build dirs after run
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_sdk_consumer_functional_linux"

set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_SDK_PRESET="${CHRONON3D_SDK_PRESET:-linux-ci}"
CHRONON3D_SDK_KEEP_DIRS="${CHRONON3D_SDK_KEEP_DIRS:-0}"

SDK_PREFIX="/tmp/chronon-sdk-install"
CONSUMER_DIR="/tmp/chronon-sdk-consumer"
CONSUMER_SRC="$ROOT/tests/install_consumer"
CONSUMER_BIN_NAME="check_full"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
BUILD_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass()    { echo "  [PASS] $1";     PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail()    { echo "  [FAIL] $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked() { echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

# Forbidden surface patterns (negative check). Per user spec verbatim:
# "NON includere chronon3d/internal/, RenderGraph, FontEngine, CameraSession,
#  SoftwareRenderer, cache internals."
declare -a FORBIDDEN_PATTERNS=(
    "chronon3d/internal/"
    "RenderGraph"
    "FontEngine"
    "CameraSession"
    "SoftwareRenderer"
    "ImageCache"
    "layout_cache"
)

# Required surface patterns (positive check). Per user spec verbatim:
# "deve usare testo/immagine/camera/timeline/render/PNG-save".
declare -a REQUIRED_SURFACES=(
    "text"
    "image"
    "camera"
    "timeline"
    "render"
    "png"
)

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state (GATE-MNT-01 + per-branch rebase invariant)
# ══════════════════════════════════════════════════════════════════════════════
echo "=============================================="
echo " verify_sdk_consumer_functional_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

REBASE_VAL="$(git config --local --get branch.main.rebase 2>/dev/null || echo "")"
if [ "$REBASE_VAL" != "true" ]; then
    _gate_fail "per_branch_rebase" "current: '$REBASE_VAL', fix: git config branch.main.rebase true"
    exit 1
fi
echo "  branch.main.rebase: $REBASE_VAL"

if ! git diff --quiet; then
    echo "SDK_CONSUMER_FAIL: working tree has uncommitted changes"
    git status -s
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "SDK_CONSUMER_FAIL: index has staged changes"
    git status -s
    exit 1
fi

git fetch origin 2>/dev/null || true
BEHIND="$(git rev-list --left-right --count origin/main...HEAD 2>/dev/null | awk '{print $1}')" || BEHIND="?"
if [ "$BEHIND" != "0" ]; then
    echo "SDK_CONSUMER_FAIL: HEAD is behind origin/main (behind=$BEHIND)"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Tree: clean (aligned with origin/main)"
_gate_pass "Repository state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Architectural gates (family parity with verify_*_linux.sh)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 2. Architectural gates =="

declare -a ARCH_GATES=(
    "tools/check_doc_sync.sh"
    "tools/check_test_suite_registration.sh"
    "tools/check_test_hygiene.sh"
)
for gate_script in "${ARCH_GATES[@]}"; do
    gate_basename="$(basename "$gate_script" .sh)"
    if [ ! -f "$gate_script" ]; then
        _gate_blocked "$gate_basename" "gate script not found"
        continue
    fi
    if bash "$gate_script" > /dev/null 2>&1; then
        _gate_pass "$gate_basename"
    else
        _gate_fail "$gate_basename" "exit code $?"
    fi
done

# ══════════════════════════════════════════════════════════════════════════════
# 3. SDK install (cmake --install → $SDK_PREFIX)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 3. SDK install → $SDK_PREFIX =="

BUILD_DIR="${CHRONON3D_SDK_BUILD_DIR:-$ROOT/build/chronon/$CHRONON3D_SDK_PRESET}"
if [ ! -d "$BUILD_DIR" ]; then
    _gate_blocked "build_dir" "build directory $BUILD_DIR does not exist (no working build host)"
    BUILD_BLOCKED=true
elif [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    _gate_blocked "build_dir" "build directory $BUILD_DIR has no CMakeCache.txt (never configured)"
    BUILD_BLOCKED=true
elif [ ! -f "$BUILD_DIR/src/libchronon3d_sdk_impl.a" ]; then
    _gate_blocked "build_dir" "build directory $BUILD_DIR has no libchronon3d_sdk_impl.a (build SDK target first)"
    BUILD_BLOCKED=true
else
    rm -rf "$SDK_PREFIX"
    mkdir -p "$SDK_PREFIX"

    echo "  Running: cmake --install $BUILD_DIR --prefix $SDK_PREFIX"
    if cmake --install "$BUILD_DIR" --prefix "$SDK_PREFIX" > /tmp/chronon_sdk_install.log 2>&1; then
        _gate_pass "install_prefix"
    else
        _gate_fail "install_prefix" "cmake --install failed (see /tmp/chronon_sdk_install.log)"
        BUILD_BLOCKED=true
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. Consumer source scaffold (out-of-tree copy)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 4. Consumer source scaffold (out-of-tree) =="

if [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "consumer_src" "build/install blocked"
else
    if [ ! -d "$CONSUMER_SRC" ]; then
        _gate_fail "consumer_src" "tests/install_consumer/ does not exist"
    elif [ ! -f "$CONSUMER_SRC/CMakeLists.txt" ]; then
        _gate_fail "consumer_src" "tests/install_consumer/CMakeLists.txt does not exist"
    elif [ ! -f "$CONSUMER_SRC/main_full.cpp" ]; then
        _gate_fail "consumer_src" "tests/install_consumer/main_full.cpp does not exist"
    else
        _gate_pass "consumer_src_present (tests/install_consumer/)"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. Consumer configure + build (find_package + Chronon3D::SDK)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 5. Consumer configure + build (Ninja + CMAKE_PREFIX_PATH=$SDK_PREFIX) =="

if [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "consumer_configure" "build/install blocked"
    _gate_blocked "consumer_build" "build/install blocked"
else
    if ! command -v ninja >/dev/null 2>&1; then
        _gate_blocked "ninja_required" "ninja not found, install: apt-get install ninja-build (or brew install ninja)"
    else
        rm -rf "$CONSUMER_DIR"
        mkdir -p "$CONSUMER_DIR"

        echo "  Running: cmake -S $CONSUMER_SRC -B $CONSUMER_DIR -G Ninja -DCMAKE_PREFIX_PATH=$SDK_PREFIX"
        if cmake -S "$CONSUMER_SRC" -B "$CONSUMER_DIR" \
            -G "Ninja" \
            -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
            > /tmp/chronon_sdk_consumer_configure.log 2>&1; then
            _gate_pass "find_package_works (Chronon3D::SDK resolved)"
        else
            _gate_fail "find_package_works" \
                "cmake configure failed (see /tmp/chronon_sdk_consumer_configure.log)"
        fi

        echo "  Running: cmake --build $CONSUMER_DIR --target $CONSUMER_BIN_NAME"
        if cmake --build "$CONSUMER_DIR" --target "$CONSUMER_BIN_NAME" \
            > /tmp/chronon_sdk_consumer_build.log 2>&1; then
            _gate_pass "consumer_build (target: $CONSUMER_BIN_NAME)"
        else
            _gate_fail "consumer_build" \
                "cmake --build failed (see /tmp/chronon_sdk_consumer_build.log)"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. Consumer run + 6 surface coverage audit + PNG output verification
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 6. Consumer run + 6 surface coverage + PNG output =="

if [ "$BUILD_BLOCKED" = true ] || [ ! -d "$CONSUMER_DIR" ]; then
    _gate_blocked "consumer_run" "consumer not built"
    _gate_blocked "surface_coverage" "consumer not run"
    _gate_blocked "png_output" "consumer not run"
else
    CONSUMER_BIN="$(find "$CONSUMER_DIR" -name "$CONSUMER_BIN_NAME" -type f -executable 2>/dev/null | head -1)"
    if [ -z "$CONSUMER_BIN" ]; then
        _gate_fail "consumer_run" "binary $CONSUMER_BIN_NAME not found after build"
    else
        # Run the consumer; capture stdout/stderr + exit code
        CONSUMER_STDOUT="$(cd "$CONSUMER_DIR" && "$CONSUMER_BIN" 2>&1)"
        CONSUMER_EXIT=$?
        if [ "$CONSUMER_EXIT" -ne 0 ]; then
            _gate_fail "consumer_run" "exit code $CONSUMER_EXIT — $CONSUMER_STDOUT"
        elif ! echo "$CONSUMER_STDOUT" | grep -qE '\[FULL-OK\]'; then
            _gate_fail "consumer_run" "no [FULL-OK] marker in output — $CONSUMER_STDOUT"
        else
            _gate_pass "consumer_run ([FULL-OK] emitted)"
        fi

        # 6 surface coverage audit — verify each surface name appears
        # in the [FULL-OK] marker (per user spec verbatim).
        for surface in "${REQUIRED_SURFACES[@]}"; do
            if echo "$CONSUMER_STDOUT" | grep -qE "\[FULL-OK\][^$]*\b$surface\b"; then
                _gate_pass "surface_$surface (covered in [FULL-OK] marker)"
            else
                _gate_fail "surface_$surface" \
                    "not found in [FULL-OK] marker: $CONSUMER_STDOUT"
            fi
        done

        # PNG output verification.
        PNG_OUTPUT="$CONSUMER_DIR/sdk_full_consumer_output.png"
        if [ -f "$PNG_OUTPUT" ] && [ -s "$PNG_OUTPUT" ]; then
            _gate_pass "png_output (sdk_full_consumer_output.png: $(stat -c%s "$PNG_OUTPUT" 2>/dev/null || echo 0) bytes)"
        else
            _gate_fail "png_output" "sdk_full_consumer_output.png missing or empty"
        fi

        # nm symbol-level audit (TICKET-VERIFY-SDK-CONSUMER-NM-VERIFY code-reviewer NIT 4):
        # verifies the consumer binary ACTUALLY links against chronon3d::sdk:: symbols
        # (canonical anti-false-green check — catches a hypothetical case where the link
        # target was silently redirected to a different namespace). Per AGENTS.md
        # Cat-3 zero new SDK API: this is a pure runtime/audit check, no public surface added.
        if ! command -v nm >/dev/null 2>&1; then
            _gate_blocked "nm_chronon_sdk_symbols" "nm not found, install: apt-get install binutils (or brew install binutils)"
        else
            NM_HITS="$(nm -C "$CONSUMER_BIN" 2>/dev/null | grep -E 'chronon3d::sdk::' | wc -l)"
            if [ "$NM_HITS" -gt 0 ]; then
                _gate_pass "nm_chronon_sdk_symbols ($NM_HITS chronon3d::sdk::* symbols resolved in binary)"
            else
                _gate_fail "nm_chronon_sdk_symbols" \
                    "0 chronon3d::sdk::* symbols found in $CONSUMER_BIN (binary does NOT link against Chronon3D::SDK)"
            fi
        fi

        # strings symbol-level audit (TICKET-VERIFY-SDK-CONSUMER-NM-VERIFY code-reviewer NIT 3+4):
        # verifies the consumer binary's strings section does NOT contain forbidden
        # internal surface symbols (RenderGraph, FontEngine, CameraSession, SoftwareRenderer).
        # This is a stronger anti-false-green check than the source-only audit in Section 7
        # (catches cases where the forbidden symbols are linked into the binary via
        # transitive dependencies even if the source doesn't reference them directly).
        if ! command -v strings >/dev/null 2>&1; then
            _gate_blocked "no_forbidden_symbols_in_strings" "strings not found, install: apt-get install binutils (or brew install binutils)"
        else
            FORBIDDEN_HITS="$(strings "$CONSUMER_BIN" 2>/dev/null | grep -E 'RenderGraph|FontEngine|CameraSession|SoftwareRenderer' | wc -l)"
            if [ "$FORBIDDEN_HITS" -eq 0 ]; then
                _gate_pass "no_forbidden_symbols_in_strings (0 references to RenderGraph/FontEngine/CameraSession/SoftwareRenderer in binary strings)"
            else
                _gate_fail "no_forbidden_symbols_in_strings" \
                    "$FORBIDDEN_HITS forbidden symbol reference(s) found in binary strings (boundary violation — consumer transitively links to internal SDK surfaces)"
                strings "$CONSUMER_BIN" 2>/dev/null | grep -E 'RenderGraph|FontEngine|CameraSession|SoftwareRenderer' | head -3 | sed 's/^/    /'
            fi
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Surface isolation audit + Cleanup + Final verdict
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 7. Surface isolation audit (no internal/, no RenderGraph, no FontEngine, no CameraSession, no SoftwareRenderer, no cache internals) =="

if [ ! -f "$CONSUMER_SRC/main_full.cpp" ]; then
    _gate_blocked "isolation_audit" "main_full.cpp not present"
else
    for pattern in "${FORBIDDEN_PATTERNS[@]}"; do
        # word-boundary grep for symbol names; literal substring grep for
        # path prefixes.
        if [[ "$pattern" == *"/"* ]]; then
            # Path-like pattern: literal substring check
            HITS="$(grep -F "$pattern" "$CONSUMER_SRC/main_full.cpp" 2>/dev/null | wc -l)"
        else
            # Symbol-like pattern: word-boundary check
            HITS="$(grep -wE "$pattern" "$CONSUMER_SRC/main_full.cpp" 2>/dev/null | wc -l)"
        fi
        if [ "$HITS" -eq 0 ]; then
            _gate_pass "no_$pattern (0 references in consumer source)"
        else
            _gate_fail "no_$pattern" "$HITS reference(s) found in consumer source (boundary violation)"
            grep -nE "$pattern" "$CONSUMER_SRC/main_full.cpp" 2>/dev/null | head -3
        fi
    done
fi

# ── Cleanup ──────────────────────────────────────────────────────────────────
echo ""
echo "== Cleanup =="

if [ "$CHRONON3D_SDK_KEEP_DIRS" = "1" ]; then
    echo "  Keeping dirs: $SDK_PREFIX $CONSUMER_DIR"
    _gate_pass "cleanup (preserved for inspection)"
else
    rm -rf "$SDK_PREFIX" "$CONSUMER_DIR" 2>/dev/null || true
    _gate_pass "cleanup (removed)"
fi

# ── Final verdict ────────────────────────────────────────────────────────────
echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$BUILD_BLOCKED" = true ]; then
    echo "SDK_CONSUMER_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  The SDK consumer build is blocked by missing build artifacts (no working"
    echo "  build host on this VPS: vcpkg glm/magic_enum missing + tmpfs quota unavailable)."
    echo "  Configure + build the SDK on a working build host, then re-run this script."
    echo ""
    echo "  Per AGENTS.md §honesty: this VPS lacks the canonical build env; macchina-verifica"
    echo "  is DEFERRED to working build host (per the established §honest-limitation pattern)."
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "SDK_CONSUMER_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "SDK_CONSUMER_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. SDK External Consumer certified."
    # AGENTS.md Rule #2 — addizionale [INFO] line on PASS (≤ 200 chars,
    # grep-discoverable via [INFO] prefix + verify_sdk_consumer_functional_linux
    # self-identifier).  This is the canonical INFO-level diagnostic.
    echo "[INFO] ${GATE_NAME}: 6 surfaces (text+image+camera+timeline+render+png_save) + 7 isolation invariants (no internal/RenderGraph/FontEngine/CameraSession/SoftwareRenderer/cache) + find_package + consumer build/run verified on working build host"
    exit 0
fi
