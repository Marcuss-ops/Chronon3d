# ═══════════════════════════════════════════════════════════════════════════
# tests/cmake/text/text_completeness.cmake
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — core text quality cluster
# (11 of the 17 ambiti grouped by domain):
#   - ambito 5:  placement
#   - ambito 6:  visible ink
#   - ambito 7:  typewriter
#   - ambito 8:  Unicode
#   - ambito 9:  alignment
#   - ambito 10: clipping (clip_bounds + text_completeness + no_clipping)
#   - ambito 11: wrapping
#   - ambito 12: style
#   - ambito 13: determinismo
#   - ambito 14: testi lunghi
#   - ambito 15: edge cases
#
# Plus the supplementary anti-false-green + golden-coverage-gaps tests
# (also bundled here because they validate the core rendering pipeline
# from the negative-complement angle).
#
# Aggregated by tests/text_golden_tests.cmake.
# ═══════════════════════════════════════════════════════════════════════════

# ambito 5 — Text Placement Golden Suite
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_placement/text_placement_golden.cpp
)
add_test(
    NAME TextPlacement
    COMMAND chronon3d_text_golden_tests --test-case="TextPlace *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 6 — P0-1 Text Visible Ink
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_visible_ink.cpp
)
add_test(
    NAME TextVisibleInk
    COMMAND chronon3d_text_golden_tests --test-case="VisibleInk *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 7 — P0-7 Text Typewriter
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_typewriter.cpp
)
add_test(
    NAME TextTypewriter
    COMMAND chronon3d_text_golden_tests --test-case="TextTypewriter *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 8 — P0-5/6 Text Unicode
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_unicode.cpp
)
add_test(
    NAME TextUnicode
    COMMAND chronon3d_text_golden_tests --test-case="TextUnicode *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 9 — P0-4 Text Alignment
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_alignment.cpp
)
add_test(
    NAME TextAlign
    COMMAND chronon3d_text_golden_tests --test-case="TextAlign *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 10 — P0-2 Text No Clipping + clip_bounds + text_completeness
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_no_clipping.cpp
        text_golden/text_clip/text_clip_bounds.cpp
        text_golden/text_clip/text_completeness.cpp
)
add_test(
    NAME TextNoClip
    COMMAND chronon3d_text_golden_tests --test-case="NoClip *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextClipBounds
    COMMAND chronon3d_text_golden_tests --test-case="Clip *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextCompleteness
    COMMAND chronon3d_text_golden_tests --test-case="TextCompleteness.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 11 — P0-3 Text Wrapping
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_wrapping.cpp
)
add_test(
    NAME TextWrapping
    COMMAND chronon3d_text_golden_tests --test-case="TextWrap *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 12 — P1-9 Text Style Properties
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_style_properties.cpp
)
add_test(
    NAME TextStyleProps
    COMMAND chronon3d_text_golden_tests --test-case="TextStyle *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 13 — P1-10 Text Determinism
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_determinism.cpp
)
add_test(
    NAME TextDeterminism
    COMMAND chronon3d_text_golden_tests --test-case="TextDeterminism *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 14 — P2-11 Text Long Text
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_long_text.cpp
)
add_test(
    NAME TextLongText
    COMMAND chronon3d_text_golden_tests --test-case="TextLongText *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ambito 15 — P2-12 Text Edge Cases
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_edge_cases.cpp
)
add_test(
    NAME TextEdgeCases
    COMMAND chronon3d_text_golden_tests --test-case="TextEdge *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Supplementary: anti-false-green + golden coverage gaps (negative-complement).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_anti_false_green.cpp
        text_golden/text_completeness/text_golden_gaps.cpp
)
add_test(
    NAME TextAntiFalseGreen
    COMMAND chronon3d_text_golden_tests --test-case="AntiFalseGreen *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextGoldenGaps
    COMMAND chronon3d_text_golden_tests --test-case="GoldenGap *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
