# ═══════════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════════
# tests/text_preset_visual_tests.cmake — PR-A4 (Blocco A, Fase 1 followup)
#
# Test executable registration for the 16 text-style presets' visual
# regression validation harness (TICKET-038 / TXT-00).
#
# STATUS (2026-06-24): Build and link: rc=0 ✅ (Agent 2 CMake registry fix).
# Test runs but fails: rc=8, FontEngine not available — environment dependency.
# See: docs/baselines/main-ccabb574-txt-00-build-green.md
#
# Canonical linker surface:
#   - chronon3d_sdk_impl  : STATIC archive aggregating ALL registry OBJECTs.
#   - chronon3d_sdk       : INTERFACE aggregate
#   - chronon3d_software  : INTERFACE umbrella
#
# Single-file test:
#   tests/text/test_text_preset_visual.cpp (~510 LOC, 128 sentinels)
#
# PNG dump pipeline is wired in follow-up sub-PR A4.png (no-op here).
# ═══════════════════════════════════════════════════════════════════════════

# TICKET-029: align to canonical Chronon3D test pattern (doctest::doctest
# + shared tests/test_main.cpp). The vcpkg `doctest` port exports only
# `doctest::doctest`, not `doctest_with_main` — previous link broke the
# cmake configure step before any TU compiled.
add_executable(chronon3d_text_preset_visual_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_visual.cpp
    ${TEST_MAIN}
)

target_include_directories(chronon3d_text_preset_visual_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(chronon3d_text_preset_visual_tests PRIVATE
    chronon3d_sdk_impl
    chronon3d_sdk
    chronon3d_software
    doctest::doctest
)

add_test(
    NAME VRTextPresetVisual
    COMMAND chronon3d_text_preset_visual_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
