# ═══════════════════════════════════════════════════════════════════════════
# tests/timeline_functional_v1_tests.cmake
#
# Timeline & Sequence V1 — anti-false-green certification test target.
#
# Materialises the 10-TEST_CASE file
# `tests/certification/test_timeline_functional_v1.cpp` as the canonical
# chronon3d_timeline_functional_v1_tests ctest target.  Per the user spec,
# the suite covers the 4 key boundary tests (resolve_local_frame(49).inactive(),
# (50).value()==0, (79).value()==29, (80).inactive()) + 6 user-spec scenarios
# (animation uses local_frame, freeze, reverse, nested sequence, overlap,
# transition).
#
# TIER=UNIT (no rendering backend, no Blend2D, no FontEngine, no GPU).
# Link contract = chronon3d_pipeline (the OBJECT aggregate of every
# per-subsystem .o) per the default UNIT tier in
# cmake/Chronon3DTestSuite.cmake:25.  Mirrors the
# `chronon3d_timeline_tests` target (tests/timeline_tests.cmake) which
# also uses TIER=UNIT for pure-struct timeline contract locks.
#
# macchina-verifica on a working build host:
#   ctest --test-dir build/chronon/linux-content-dev \
#     -R chronon3d_timeline_functional_v1_tests --output-on-failure
# expects 10/10 PASS (4 boundary + 6 scenario).
#
# Per ADR-018, this file is also listed in CMAKE_CONFIGURE_DEPENDS
# at the top of tests/CMakeLists.txt.
# ═══════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_timeline_functional_v1_tests
    TIER UNIT
    SOURCES certification/test_timeline_functional_v1.cpp
)
