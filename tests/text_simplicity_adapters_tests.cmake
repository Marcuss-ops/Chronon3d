# ─── M1.8 §6 TICKET-SIMPLICITY-ADAPTERS tests (chronon3d_text_simplicity_adapters_tests) ───
#
# Adds the §6 validation test target. This test target covers:
#   1. centered_text() deprecation (compile + runtime spdlog::warn)
#   2. glow_text() consolidation into TextDefinition.effects.glow
#   3. LayerBuilder::text_run() backward-compat (single-choke-point)
#   4. Old API vs new API byte-equivalence (render tier, gated)
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.

set(_TEXT_SIMPLICITY_ADAPTERS_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_simplicity_adapters.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_text_simplicity_adapters_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_simplicity_adapters.cpp
)

# Wire into the FAST test aggregator (math + struct + diagnostic, no rendering
# backend required for tests #1-6 + #10). Mirrors the pattern used for
# chronon3d_text_definition_tests + chronon3d_safe_area_placement_tests
# + chronon3d_text_builder_ergonomics_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_simplicity_adapters_tests)
