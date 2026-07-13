#!/bin/bash
set +e

echo "=== STAGE 0: git state sanity ==="
echo "  HEAD = $(git rev-parse HEAD)"
echo "  @u   = $(git rev-parse '@{u}')"
echo ""

cd /home/pierone/Pyt/Chronon3d

echo "=== STAGE 0.5: nuke pre-existing build dir to wipe ALL stale state ==="
# Per thinker verdict Path V: rm -rf is the only way to guarantee both
# CMake cache AND vcpkg_installed/ artifacts inside the build tree are
# purged of stale references (e.g. deleted `visual/ae_parity/ae_parity_scenes.cpp`,
# stale toolchain pointers after --fresh, etc.).
rm -rf build/manual-test
echo "  done"

echo "=== STAGE 1: cmake preset invocation (Path V: canonical + tests enabled) ==="
# Path V cmd: cmake --preset linux-fast-dev -B build/manual-test -DCHRONON3D_BUILD_TESTS=ON
# - --preset wires the canonical vcpkg toolchain wrapper transparently (vs. hand-rolling flags).
# - -B overrides the preset's default binaryDir so artifacts land at the script-expected path.
# - -DCHRONON3D_BUILD_TESTS=ON enables target_sources(chronon3d_core_tests ...).
cmake --preset linux-fast-dev -B build/manual-test -DCHRONON3D_BUILD_TESTS=ON 2>&1 | tail -30
RC_CONF=${PIPESTATUS[0]}
echo "rc=$RC_CONF"
if [ "$RC_CONF" -ne 0 ]; then
    echo "RECONFIGURE FAILED rc=$RC_CONF"
    exit "$RC_CONF"
fi
echo ""

echo "=== STAGE 2: build target chronon3d_core_tests ==="
cmake --build build/manual-test --target chronon3d_core_tests -j4 2>&1 | tail -30
RC_BUILD=${PIPESTATUS[0]}
echo "rc=$RC_BUILD"
if [ "$RC_BUILD" -ne 0 ]; then
    echo "BUILD FAILED rc=$RC_BUILD"
    exit "$RC_BUILD"
fi
echo ""

echo "=== STAGE 3: pre-ctest staleness check (AGENTS.md invariant) ==="
TEST_BIN=build/manual-test/tests/chronon3d_core_tests
SRC=tests/text/test_anim_typewriter_error_path.cpp
if [ ! -x "$TEST_BIN" ]; then
    echo "STALE BUILD: binary missing at $TEST_BIN"
    exit 1
fi
if [ "$SRC" -nt "$TEST_BIN" ]; then
    echo "STALE BUILD: $SRC newer than $TEST_BIN"
    exit 1
fi
echo "  binary fresh"
echo ""

echo "=== STAGE 4: ctest -R Azione 18 (filtered) ==="
cd build/manual-test || exit 1
ctest -R "Azione 18" --output-on-failure 2>&1 | tail -40
RC_CTEST=${PIPESTATUS[0]}
echo "rc=$RC_CTEST"
if [ "$RC_CTEST" -ne 0 ]; then
    echo "CTEST FAILED rc=$RC_CTEST"
    exit "$RC_CTEST"
fi
echo ""

echo "=== STAGE 5: full chronon3d_core_tests sanity ==="
ctest -R chronon3d_core_tests --output-on-failure 2>&1 | tail -10
RC_FULL=${PIPESTATUS[0]}
echo "rc=$RC_FULL"
if [ "$RC_FULL" -ne 0 ]; then
    echo "FULL CTEST FAILED rc=$RC_FULL"
    exit "$RC_FULL"
fi
echo ""

echo "=== ALL STAGES GREEN ==="
exit 0
