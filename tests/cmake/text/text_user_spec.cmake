# ═══════════════════════════════════════════════════════════════════════════
# tests/cmake/text/text_user_spec.cmake
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — user-spec subset of the text
# golden test suite (ADR-014 Decision 1: 12 user-spec golden tests,
# TXT-QA-01 visual group).
#
# Aggregated by tests/text_golden_tests.cmake (the only file that
# defines the chronon3d_text_golden_tests executable; this file only
# adds target_sources() + add_test() calls).
# ═══════════════════════════════════════════════════════════════════════════

# ADR-014 Decision 1 — 12 user-spec golden tests (TXT-QA-01 visual group).
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

# Filter ctest -R to the user-spec subset only (per ADR-014 Decision 3).
add_test(
    NAME TextGoldenUserSpec
    COMMAND chronon3d_text_golden_tests --test-case="UserSpec*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
