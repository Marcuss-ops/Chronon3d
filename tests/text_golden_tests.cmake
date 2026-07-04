# ═══════════════════════════════════════════════════════════════════════════
# tests/text_golden_tests.cmake — TXT-QA-01 Real Golden Text Harness
#
# Standalone executable for the text golden regression test suite.
# Kept separate from chronon3d_text_preset_visual_tests to avoid
# unity-build conflicts between golden_test.cpp / image_diff.cpp
# and the text visual harness includes.
#
# Golden PNGs:   test_renders/golden/text/
# Artifacts:     test_renders/artifacts/text/
#
# Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGolden
# ═══════════════════════════════════════════════════════════════════════════

add_executable(chronon3d_text_golden_tests
    ${TEST_MAIN}
    text/test_text_golden.cpp
    visual/support/golden_test.cpp
    visual/support/image_diff.cpp
)

target_link_libraries(chronon3d_text_golden_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_software
        doctest::doctest
)

target_compile_definitions(chronon3d_text_golden_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_text_golden_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_text_golden_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_text_golden_tests)

# ADR-014 Decision 1 — 12 user-spec golden tests (TXT-QA-01 visual group).
# Appended directly to the existing target via target_sources() to avoid
# duplicating the harness include chain. UNITY_BUILD OFF keeps per-test
# translation units independent (no ODR collisions on lambda captures).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/user_spec/01_text_basic_centered.cpp
        text_golden/user_spec/02_font_swap_same_text.cpp
        text_golden/user_spec/03_multifont_single_line.cpp
        text_golden/user_spec/04_multifont_middle_run_failure.cpp
        text_golden/user_spec/05_bidi_english_arabic_mixed.cpp
        text_golden/user_spec/06_text_wrap_narrow_box.cpp
        text_golden/user_spec/07_vertical_short_safe_area.cpp
        text_golden/user_spec/08_anim_typewriter_character.cpp
        text_golden/user_spec/09_anim_character_offset_wave.cpp
        text_golden/user_spec/10_text_fill_stroke.cpp
        text_golden/user_spec/11_aspect_ratio_layout.cpp
        text_golden/user_spec/12_anim_framerate_determinism.cpp
)

add_test(
    NAME TextGolden
    COMMAND chronon3d_text_golden_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
# Filter ctest -R to the user-spec subset only (per ADR-014 Decision 3:
# bash driver / CI use this regex; the full chronon3d_text_golden_tests
# target name still works for the original TXT-QA-01 cases).
add_test(
    NAME TextGoldenUserSpec
    COMMAND chronon3d_text_golden_tests --test-case="UserSpec*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
