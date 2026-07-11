# ─── M1.8 §6 TICKET-SIMPLICITY-ADAPTERS tests (chronon3d_text_simplicity_adapters_tests) ───
#
# Adds the §6 validation test target. This test target covers:
#   1. centered_text() deprecation (compile + runtime spdlog::warn)
#   2. glow_text() consolidation into TextDefinition.style.material
#   3. LayerBuilder::text_run() backward-compat (single-choke-point)
#   4. Old API vs new API pixel-hash equivalence (render tier, gated by
#      CHRONON3D_USE_BLEND2D)
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.

set(_TEXT_SIMPLICITY_ADAPTERS_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_simplicity_adapters.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_text_simplicity_adapters_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk
                  chronon3d_software
                  chronon3d_runtime
                  chronon3d_backend_image
                  chronon3d_content
                  chronon3d_text_core
                  chronon3d_scene
                  chronon3d_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_simplicity_adapters.cpp
)

# NOTE: this target is intentionally NOT added to CHRONON3D_FAST_TEST_DEPS
# because the render parity tests exercise the full software renderer and
# framebuffer pipeline. The math/diagnostic tests above still run quickly,
# but the target as a whole belongs with the integration test suite.
