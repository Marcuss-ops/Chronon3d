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
# Single-file test:
#   tests/text/test_text_preset_visual.cpp (~510 LOC, 128 sentinels)
#
# PNG dump pipeline is wired in follow-up sub-PR A4.png (no-op here).
# ═══════════════════════════════════════════════════════════════════════════

add_executable(chronon3d_text_preset_visual_tests
    ${TEST_MAIN}
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_visual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
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
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
