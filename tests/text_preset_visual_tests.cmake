# ═══════════════════════════════════════════════════════════════════════════
# tests/text_preset_visual_tests.cmake — PR-A4 (Blocco A, Fase 1 followup)
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()
# Migrated to chronon3d_add_test_suite(TIER INTEGRATION) (this commit,
# closing the §11.1 migration backlog).
#
# Phase-2.1 P0 split — SINGLE executable, FOUR compiled sources:
#   * tests/text/test_text_preset_reveal.cpp     (10 Reveal presets)
#   * tests/text/test_text_preset_emphasis.cpp   (4  Emphasis presets)
#   * tests/text/test_text_preset_subtitle.cpp   (2 Subtitle + 2 e2e)
#   * tests/text/test_text_preset_cinematic.cpp  (placeholder for A4.1
#                                                  cinematic tier)
# The four files #include the shared headers from tests/text/visual/:
#   * text_visual_fixture.hpp        (composition build + render + gate)
#   * text_visual_metrics.cpp        (RectF / ScenarioMetrics PODs +
#                                     compute_metrics() — #include'd from
#                                     each split TU via fixture.hpp's
#                                     bottom #include)
#   * text_visual_expectations.hpp   (VisualExpectation enum + gate macro)
#   * text_visual_sentinels.hpp      (128 sentinel constants — data only)
#
# PNG dump pipeline is wired in follow-up sub-PR A4.png (no-op here).
# ═══════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_text_preset_visual_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_software
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_reveal.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_emphasis.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_subtitle.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_cinematic.cpp
)
