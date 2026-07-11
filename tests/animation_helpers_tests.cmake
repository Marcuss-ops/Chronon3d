# ─── M1.8 §3A TICKET-SIMPLICITY-ANIMATION tests (chronon3d_animation_helpers_tests) ───
#
# Adds the §3A validation test target. This test target covers:
#   1. Locks ALL 17 animation helpers in `interpolate.hpp` with 17 × 2 = 34
#      doctest CHECK assertions (determinism + edge case per helper).
#   2. The 6 NEW helpers (M1.8 §3A extension) are tested alongside the
#      pre-existing 11 helpers — additive-only coverage, no duplication.
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.

set(_ANIMATION_HELPERS_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/animations/test_animation_helpers.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_animation_helpers_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/animations/test_animation_helpers.cpp
)

# Wire into the FAST test aggregator (pure math harness, no rendering
# backend required). Mirrors the pattern used for chronon3d_text_definition_tests
# + chronon3d_safe_area_placement_tests + chronon3d_text_builder_ergonomics_tests
# + chronon3d_text_simplicity_adapters_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_animation_helpers_tests)
