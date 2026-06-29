# ═══════════════════════════════════════════════════════════════════════════
# tests/text_preset_visual_tests.cmake — PR-A4 (Blocco A, Fase 1 followup)
#
# Test executable registration for the 16 text-style presets' visual
# regression validation harness. Per cmake/Chronon3DRegistry.cmake
# (CHRONON3D_REGISTRY_INTERFACE_LIBS), the canonical linker surface for
# tests covering sdk + software-render path is:
#   - chronon3d_sdk       : interface lib, drags scene + runtime + text
#   - chronon3d_software  : software-backend grouper (in INTERFACE_LIBS)
# Per tests/CMakeLists.txt pattern, `chronon3d_sdk` is the aggregator used
# by every cross-cutting test target.
#
# Phase-2.1 P0 split — SINGLE executable, FIVE compiled sources:
#   * tests/text/test_text_preset_reveal.cpp     (10 Reveal presets)
#   * tests/text/test_text_preset_emphasis.cpp   (4  Emphasis presets)
#   * tests/text/test_text_preset_subtitle.cpp   (2 Subtitle + 2 e2e)
#   * tests/text/test_text_preset_cinematic.cpp  (placeholder for A4.1
#                                                  cinematic tier)
# The five files #include the shared headers from tests/text/visual/:
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

# TICKET-029: align to canonical Chronon3D test pattern (doctest::doctest
# + shared tests/test_main.cpp). The vcpkg `doctest` port exports only
# `doctest::doctest`, not `doctest_with_main` — previous link broke the
# cmake configure step before any TU compiled.
add_executable(chronon3d_text_preset_visual_tests
    ${TEST_MAIN}
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_reveal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_emphasis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_subtitle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_cinematic.cpp
)

target_include_directories(chronon3d_text_preset_visual_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(chronon3d_text_preset_visual_tests PRIVATE
    chronon3d_sdk
    chronon3d_software
    doctest::doctest
)

add_test(
    NAME VRTextPresetVisual
    COMMAND chronon3d_text_preset_visual_tests
)
