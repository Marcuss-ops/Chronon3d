# tests/presets_golden_tests.cmake
# ─────────────────────────────────────────────────────────────────────
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# Replaces the pre-refactor body-wrap `if(CHRONON3D_USE_BLEND2D AND
# CHRON3D_ENABLE_TEXT) ... endif()` (now removed from this file).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

# TICKET-SIMPLICITY-PRESETS §3C — Golden frame regression for the 5
# reusable TextDefinition-producing presets (title_centered,
# subtitle_bottom, caption_safe_area, kinetic_word, lower_third).
#
# Highlights:
#   - 5 PNG goldens in `test_renders/golden/text/presets/`:
#       1. preset_title_centered_1920x1080_F0.png
#       2. preset_subtitle_bottom_1920x1080_F0.png
#       3. preset_caption_safe_area_1920x1080_F0.png
#       4. preset_kinetic_word_1920x1080_F0.png
#       5. preset_lower_third_1920x1080_F0.png
#   - TIER=INTEGRATION (per ADR-018 convention: visual regression
#     tests are INTEGRATION tier).
#   - Re-bake: CHRONON3D_UPDATE_GOLDENS=1 ctest -R
#     chronon3d_presets_golden_tests  (deferred to working build
#     host per AGENTS.md §honesty; the test gracefully skips on
#     missing golden via the `result.golden_missing` short-circuit).
#
# Registration helper: chronon3d_register_test_source() (cmake/
# Chronon3DTestSuite.cmake) so the §12 Python gate
# (tools/check_test_source_registration.py) tracks this file under
# the canonical test-source registry.
# ─────────────────────────────────────────────────────────────────────

chronon3d_add_test_suite(
    NAME chronon3d_presets_golden_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_content
                  chronon3d_runtime chronon3d_text_core
    SOURCES
        text_golden/presets/test_presets_golden.cpp
        visual/support/golden_test.cpp
        visual/support/image_diff.cpp
)
target_compile_definitions(chronon3d_presets_golden_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(
    NAME PresetsGolden
    COMMAND chronon3d_presets_golden_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
# §12.1 test source registration (canonical).
if(COMMAND chronon3d_register_test_source)
    chronon3d_register_test_source(
        "tests/text_golden/presets/test_presets_golden.cpp"
    )
endif()
