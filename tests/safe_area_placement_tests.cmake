# ─── M1.8 §3B SafeArea placement tests (chronon3d_safe_area_placement_tests) ───
#
# Adds the SafeAreaEnum + SafeArea{Left,Right} extension test executable.
# This test target is:
#   - pure math harness (no Framebuffer / compositor / GPU);
#   - UNGATED (compiles in any preset that turns on CHRONON3D_BUILD_TESTS);
#   - mirrors the pattern of text_definition_tests.cmake (M1.8 §3) and
#     text_bbox_math_tests.cmake (FU03) so the safe-area family has the
#     same lock surface as the rest of the text V1 workstream.
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.

set(_SAFE_AREA_PLACEMENT_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_safe_area_placement.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_safe_area_placement_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_safe_area_placement.cpp
)

# Wire into the FAST test aggregator (pure math, no rendering backend).
# Mirrors the pattern used for chronon3d_text_definition_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_safe_area_placement_tests)
