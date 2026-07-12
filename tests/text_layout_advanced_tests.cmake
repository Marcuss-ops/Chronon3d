# ─── Advanced layout matrix tests (chronon3d_text_layout_advanced_tests) ───
#
# TICKET-FASE4-LAYOUT closure: advanced layout matrix + adversarial tests
# (mirrors the test_safe_area_placement_tests.cmake pattern).
#
# Scope:
#   1. 3×3 wrap × overflow matrix (TextWrap::None/Word/Character ×
#      TextOverflow::Clip/Ellipsis/Visible) — 9 modes
#   2. line-height matrix (0.5/0.8/1.0/1.2/1.5/2.0/3.0 × 3-line text) —
#      7 values + monotonicity check
#   3. Adversarial: long-word (one word > box width) × 3 wrap modes
#   4. Adversarial: sub-glyph box (box width < one glyph) × 3 overflow modes
#   5. Adversarial: ellipsis-without-space (no whitespace + ellipsis) × 3 cases
#
# This test target is:
#   - pure layout math (no rendering / no Blend2D dependency);
#   - UNGATED (compiles in any preset that turns on CHRONON3D_BUILD_TESTS);
#   - mirrors the pattern of safe_area_placement_tests.cmake so the
#     advanced-layout family has the same lock surface as the rest of
#     the text V1 workstream.
#
# Per ADR-018, this .cmake file is also listed in CMAKE_CONFIGURE_DEPENDS
# at the top of tests/CMakeLists.txt so Ninja / Make re-run cmake when
# this file is touched.

chronon3d_add_test_suite(
    NAME chronon3d_text_layout_advanced_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core chronon3d_sdk
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_advanced_layout_matrix.cpp
)

# Wire into the FAST test aggregator (pure layout math, no rendering
# backend).  Mirrors the pattern used for chronon3d_text_definition_tests
# + chronon3d_safe_area_placement_tests + chronon3d_text_builder_ergonomics_tests
# + chronon3d_animation_helpers_tests + chronon3d_text_simplicity_adapters_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_layout_advanced_tests)
