# ─── M1.8 §5 Builder ergonomics tests (chronon3d_text_builder_ergonomics_tests) ───
#
# Adds the TICKET-SIMPLICITY-BUILDER "centra un titolo in < 10 righe"
# criterion validation test target.  This test target is:
#   - pure math + struct inspection (no Framebuffer / compositor / GPU);
#   - UNGATED (compiles in any preset that turns on CHRONON3D_BUILD_TESTS);
#   - mirrors the pattern of text_definition_tests.cmake + safe_area_placement_tests.cmake
#     so the ergonomic surface has the same lock surface as the rest of the
#     text V1 workstream.
#
# What this locks:
#   1. The canonical centered-title chain on `chronon3d::authoring::Text`
#      uses ≤ 10 method calls (literal: 4 — `Layer::text()` + `.content()`
#      + `.font(path, size)` + `.place(CanvasCenter)`). Regression lock
#      against insertion of intermediate boilerplate.
#   2. `.place(CanvasCenter)` routes through the canonical resolver and
#      produces a PendingTextRun whose bbox is ≤ 1px from the
#      mathematical canvas center (960, 540) on a 1920×1080 canvas.
#      Cross-checked against `resolve_placement_origin()` independently.
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.

set(_TEXT_BUILDER_ERGONOMICS_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_builder_ergonomics.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_text_builder_ergonomics_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_builder_ergonomics.cpp
)

# Wire into the FAST test aggregator (pure math + struct inspection,
# no rendering backend).  Mirrors the pattern used for
# chronon3d_text_definition_tests + chronon3d_safe_area_placement_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_builder_ergonomics_tests)

doctest_discover_tests(chronon3d_text_builder_ergonomics_tests
    PROPERTIES
        TIMEOUT 30
)
