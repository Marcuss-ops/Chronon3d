# ═══════════════════════════════════════════════════════════════════════════
# tests/cmake/text/text_animation.cmake
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — animations cluster
# (ambito 17 of the 17: 2 TICKET-FASE2-TRANSFORMS-ANIMATION §10 animation
# tests — position + opacity).
#
# Aggregated by tests/text_golden_tests.cmake.
# ═══════════════════════════════════════════════════════════════════════════

# anim_01_position — frame-by-frame linear X translation.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/anim_01_position.cpp
)
add_test(
    NAME TextAnimPosition
    COMMAND chronon3d_text_golden_tests --test-case="TextAnim.Position_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# anim_02_opacity — frame-by-frame opacity envelope.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/anim_02_opacity.cpp
)
add_test(
    NAME TextAnimOpacity
    COMMAND chronon3d_text_golden_tests --test-case="TextAnim.Opacity_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TRN-04 — AnimatedTextDocument DissolveLayouts golden coverage
# (0%, 25%, 50%, 75%, 100% of a 30-frame dissolve).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_dissolve/text_dissolve.cpp
)
add_test(
    NAME TextDissolve
    COMMAND chronon3d_text_golden_tests --test-case="TextDissolve.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
