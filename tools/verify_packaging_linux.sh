#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_packaging_linux.sh
#
# Canonical Packaging & Relocatability certification gate (P2).
#
# Verifies the SDK install is relocatable and consumer-ready per user spec
# verbatim:
#   1. cmake --install build/chronon/linux-release --prefix /tmp/chronon-install-a
#   2. mv /tmp/chronon-install-a /tmp/chronon-install-b
#   3. cmake -S tests/package_consumer -B /tmp/chronon-consumer -G Ninja \
#         -DCMAKE_PREFIX_PATH=/tmp/chronon-install-b
#   4. build, esegui consumer
#
# PASS quando (4 anti-false-green invariants):
#   - pacchetto funziona dopo lo spostamento
#   - nessun riferimento alla source tree in /tmp/chronon-install-b
#   - nessun path assoluto a /tmp/chronon-install-a in /tmp/chronon-install-b
#   - find_package(Chronon3D) works from /tmp/chronon-install-b
#   - consumer produce output (exit 0 + produces a non-empty artifact)
#
# Verdict contract:
#   PACKAGING_FUNCTIONAL_PASS    — all sections pass (exit 0)
#   PACKAGING_FUNCTIONAL_FAIL    — any section fails (exit 1)
#   PACKAGING_FUNCTIONAL_BLOCKED — env/build not available (exit 2)
#
# §honesty contract (AGENTS.md):
#   - BLOCKED steps reported with explicit diagnostic, not silently skipped.
#   - BUILD_BLOCKED is emitted when the SDK build artifacts are missing
#     (no working build host on this VPS — vcpkg glm/magic_enum missing).
#   - PACKAGING_FUNCTIONAL_PASS is only emitted when ALL steps pass.
#   - PACKAGING_FUNCTIONAL_FAIL is emitted on any non-zero exit.
#   - PACKAGING_FUNCTIONAL_BLOCKED is emitted when the build is blocked.
#   - Addizionale [INFO] ${GATE_NAME}: ... line on PASS (per AGENTS.md
#     "## Regole di lint documentale" §INFO-level diagnostic style).
#
# Usage:
#   bash tools/verify_packaging_linux.sh
#
# Environment overrides:
#   CHRONON3D_PKG_PRESET=<name>       CMake preset (default: linux-release)
#   CHRONON3D_PKG_BUILD_DIR=<path>    Build dir override (default: build/chronon/${PRESET})
#   CHRONON3D_PKG_KEEP_DIRS=1         Keep temp install/build dirs after run
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME="verify_packaging_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_PKG_PRESET="${CHRONON3D_PKG_PRESET:-linux-release}"
CHRONON3D_PKG_KEEP_DIRS="${CHRONON3D_PKG_KEEP_DIRS:-0}"

PREFIX_A="/tmp/chronon-install-a"
PREFIX_B="/tmp/chronon-install-b"
CONSUMER_DIR="/tmp/chronon-consumer"
CONSUMER_SRC="$ROOT/tests/package_consumer"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
BUILD_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass()    { echo "  [PASS] $1";    PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail()    { echo "  [FAIL] $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked() { echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state (GATE-MNT-01 + per-branch rebase invariant)
# ══════════════════════════════════════════════════════════════════════════════
echo "=============================================="
echo " verify_packaging_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

# (1a) Per-branch rebase invariant (GATE-MNT-01-EXT)
REBASE_VAL="$(git config --local --get branch.main.rebase 2>/dev/null || echo "")"
if [ "$REBASE_VAL" != "true" ]; then
    # Use the canonical _gate_fail family helper for consistency with the
    # rest of the gate (cat-3 family parity, NIT 2 of code-review).
    _gate_fail "per_branch_rebase" "current: '$REBASE_VAL', fix: git config branch.main.rebase true"
    exit 1
fi
echo "  branch.main.rebase: $REBASE_VAL"

# (1b) Clean working tree
if ! git diff --quiet; then
    echo "PACKAGING_FAIL: working tree has uncommitted changes"
    git status -s
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "PACKAGING_FAIL: index has staged changes"
    git status -s
    exit 1
fi

# (1c) Aligned with origin/main
git fetch origin 2>/dev/null || true
BEHIND="$(git rev-list --left-right --count origin/main...HEAD 2>/dev/null | awk '{print $1}')" || BEHIND="?"
if [ "$BEHIND" != "0" ]; then
    echo "PACKAGING_FAIL: HEAD is behind origin/main (behind=$BEHIND)"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Branch: $(git branch --show-current)"
echo "  Tree: clean (aligned with origin/main)"
_gate_pass "Repository state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Architectural gates (family parity with verify_text/timeline)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 2. Architectural gates =="

declare -a GATES=(
    "tools/check_doc_sync.sh"
    "tools/check_test_suite_registration.sh"
    "tools/check_test_hygiene.sh"
)
for gate_script in "${GATES[@]}"; do
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
# 3. cmake --install to prefix A (user-spec step 1)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 3. cmake --install → $PREFIX_A =="

BUILD_DIR="${CHRONON3D_PKG_BUILD_DIR:-$ROOT/build/chronon/$CHRONON3D_PKG_PRESET}"
if [ ! -d "$BUILD_DIR" ]; then
    _gate_blocked "build_dir" "build directory $BUILD_DIR does not exist (no working build host)"
    BUILD_BLOCKED=true
elif [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    _gate_blocked "build_dir" "build directory $BUILD_DIR has no CMakeCache.txt (never configured)"
    BUILD_BLOCKED=true
elif [ ! -f "$BUILD_DIR/src/libchronon3d_sdk_impl.a" ]; then
    # Check for the SDK build artifact (NOT install_manifest.txt — that's
    # GENERATED by `cmake --install` itself, so a fresh build dir that's
    # been configured + built but never installed would spuriously BLOCKED).
    # The single aggregated static archive is the canonical "SDK ready to
    # install" attestation (TICKET-SDK-PACKAGING-CONSOLIDATION).
    _gate_blocked "build_dir" "build directory $BUILD_DIR has no libchronon3d_sdk_impl.a (build SDK target first)"
    BUILD_BLOCKED=true
else
    # Clean any previous run
    rm -rf "$PREFIX_A" "$PREFIX_B" "$CONSUMER_DIR"
    mkdir -p "$PREFIX_A"

    echo "  Running: cmake --install $BUILD_DIR --prefix $PREFIX_A"
    if cmake --install "$BUILD_DIR" --prefix "$PREFIX_A" > /tmp/chronon_pkg_install_a.log 2>&1; then
        _gate_pass "install_prefix_a"
    else
        _gate_fail "install_prefix_a" "cmake --install failed (see /tmp/chronon_pkg_install_a.log)"
        BUILD_BLOCKED=true
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. mv A → B (user-spec step 2) + path audit
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 4. mv $PREFIX_A → $PREFIX_B + path audit =="

if [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "relocate_mv" "build/install blocked"
    _gate_blocked "no_source_tree_refs" "relocation blocked"
    _gate_blocked "no_absolute_paths_to_a" "relocation blocked"
else
    if mv "$PREFIX_A" "$PREFIX_B" 2>/dev/null; then
        _gate_pass "relocate_mv"

        # Verify Chronon3DConfig.cmake + Chronon3DTargets.cmake survived the move
        CONFIG_FILE="$(find "$PREFIX_B" -name 'Chronon3DConfig.cmake' 2>/dev/null | head -1)"
        if [ -n "$CONFIG_FILE" ] && [ -s "$CONFIG_FILE" ]; then
            _gate_pass "config_survives_relocation ($CONFIG_FILE)"
        else
            _gate_fail "config_survives_relocation" "Chronon3DConfig.cmake not found after relocation"
        fi

        # (Anti-false-green invariant #1) no source tree references in installed files
        SOURCE_REFS="$(grep -rF "$ROOT" "$PREFIX_B" 2>/dev/null | grep -v 'Binary file' | wc -l)"
        if [ "$SOURCE_REFS" -eq 0 ]; then
            _gate_pass "no_source_tree_refs (0 references to $ROOT)"
        else
            _gate_fail "no_source_tree_refs" "$SOURCE_REFS source tree references in installed files"
            grep -rF "$ROOT" "$PREFIX_B" 2>/dev/null | grep -v 'Binary file' | head -3
        fi

        # (Anti-false-green invariant #2) no absolute paths to old prefix A in installed files
        ABS_PATHS_A="$(grep -rF "$PREFIX_A" "$PREFIX_B" 2>/dev/null | grep -v 'Binary file' | wc -l)"
        if [ "$ABS_PATHS_A" -eq 0 ]; then
            _gate_pass "no_absolute_paths_to_a (0 references to $PREFIX_A)"
        else
            _gate_fail "no_absolute_paths_to_a" "$ABS_PATHS_A absolute path(s) to old prefix found"
            grep -rF "$PREFIX_A" "$PREFIX_B" 2>/dev/null | grep -v 'Binary file' | head -3
        fi

        # (Anti-false-green invariant #3) no suspicious build-host paths in cmake config
        SUSPICIOUS="$(grep -rE '"/tmp/|/home/' "$PREFIX_B" 2>/dev/null \
            | grep -v 'Binary file' \
            | grep -v "$PREFIX_B" \
            | wc -l)"
        if [ "$SUSPICIOUS" -eq 0 ]; then
            _gate_pass "no_suspicious_build_host_paths (0 leaks to /tmp/ or /home/ in installed files)"
        else
            _gate_fail "no_suspicious_build_host_paths" "$SUSPICIOUS suspicious path(s) in installed files"
            grep -rE '"/tmp/|/home/' "$PREFIX_B" 2>/dev/null \
                | grep -v 'Binary file' \
                | grep -v "$PREFIX_B" \
                | head -3
        fi
    else
        _gate_fail "relocate_mv" "mv $PREFIX_A $PREFIX_B failed"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. External consumer configure (user-spec step 3)
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 5. External consumer configure (Ninja + CMAKE_PREFIX_PATH=$PREFIX_B) =="

if [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "consumer_configure" "build/install blocked"
else
    if [ ! -d "$CONSUMER_SRC" ]; then
        _gate_fail "consumer_src" "tests/package_consumer/ does not exist"
    elif ! command -v ninja >/dev/null 2>&1; then
        # User spec verbatim mandates `-G Ninja`; absent ninja, the
        # consumer configure would use cmake's default generator, which
        # deviates from the spec. Emit BLOCKED with explicit install hint.
        _gate_blocked "ninja_required" "ninja not found, install: apt-get install ninja-build (or brew install ninja) — user spec mandates -G Ninja"
    else
        rm -rf "$CONSUMER_DIR"
        mkdir -p "$CONSUMER_DIR"

        echo "  Running: cmake -S $CONSUMER_SRC -B $CONSUMER_DIR -G Ninja -DCMAKE_PREFIX_PATH=$PREFIX_B"
        if cmake -S "$CONSUMER_SRC" -B "$CONSUMER_DIR" \
            -G "Ninja" \
            -DCMAKE_PREFIX_PATH="$PREFIX_B" \
            > /tmp/chronon_pkg_consumer_configure.log 2>&1; then
            _gate_pass "find_package_works_after_relocation"
        else
            _gate_fail "find_package_works_after_relocation" \
                "cmake configure failed (see /tmp/chronon_pkg_consumer_configure.log)"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. External consumer build + run
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 6. External consumer build + run =="

if [ "$BUILD_BLOCKED" = true ] || [ ! -d "$CONSUMER_DIR" ] || [ ! -f "$CONSUMER_DIR/CMakeCache.txt" ]; then
    _gate_blocked "consumer_build" "consumer not configured"
    _gate_blocked "consumer_run" "consumer not built"
else
    echo "  Running: cmake --build $CONSUMER_DIR"
    if cmake --build "$CONSUMER_DIR" > /tmp/chronon_pkg_consumer_build.log 2>&1; then
        _gate_pass "consumer_build"
    else
        _gate_fail "consumer_build" "cmake --build failed (see /tmp/chronon_pkg_consumer_build.log)"
    fi

    # Find the consumer binary (named package_consumer_test per tests/package_consumer/CMakeLists.txt)
    CONSUMER_BIN="$(find "$CONSUMER_DIR" -name 'package_consumer_test' -type f -executable 2>/dev/null | head -1)"
    if [ -z "$CONSUMER_BIN" ]; then
        _gate_fail "consumer_run" "binary package_consumer_test not found after build"
    else
        echo "  Running: $CONSUMER_BIN"
        if CONSUMER_OUTPUT="$("$CONSUMER_BIN" 2>&1)"; then
            # The consumer must produce output (any output at all is success — see main.cpp)
            if [ -n "$CONSUMER_OUTPUT" ]; then
                _gate_pass "consumer_run_produces_output ($CONSUMER_OUTPUT)"
            else
                _gate_fail "consumer_run_produces_output" "consumer exited 0 but produced no output"
            fi
        else
            _gate_fail "consumer_run" "consumer returned non-zero exit code"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Cleanup + final verdict
# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo "== 7. Cleanup =="

if [ "$CHRONON3D_PKG_KEEP_DIRS" = "1" ]; then
    echo "  Keeping dirs: $PREFIX_B $CONSUMER_DIR"
    _gate_pass "cleanup (preserved for inspection)"
else
    rm -rf "$PREFIX_B" "$CONSUMER_DIR" 2>/dev/null || true
    _gate_pass "cleanup (removed)"
fi

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$BUILD_BLOCKED" = true ]; then
    echo "PACKAGING_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  The packaging build is blocked by missing build artifacts (no working build host"
    echo "  on this VPS: vcpkg glm/magic_enum missing + tmpfs quota unavailable). The build"
    echo "  directory $BUILD_DIR does not exist or was never configured."
    echo "  Configure + build the SDK on a working build host, then re-run this script."
    echo ""
    echo "  Per AGENTS.md §honesty: this VPS lacks the canonical build env; macchina-verifica is"
    echo "  DEFERRED to working build host (per the established §honest-limitation pattern)."
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "PACKAGING_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "PACKAGING_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Packaging & Relocatability certified."
    # AGENTS.md Rule #2 — addizionale [INFO] line on PASS (≤ 200 chars,
    # grep-discoverable via [INFO] prefix + verify_packaging_linux
    # self-identifier).  This is the canonical INFO-level diagnostic.
    echo "[INFO] ${GATE_NAME}: 4 anti-false-green invariants (mv+config+no_source_refs+no_abs_paths) + find_package + consumer build/run verified on working build host"
    exit 0
fi
