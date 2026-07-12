# ═══════════════════════════════════════════════════════════════════════════
# tests/cmake/text/text_ae_parity.cmake
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — AE parity cinematic cluster
# (3 of the 17 ambiti grouped by domain):
#   - ambito 2: AE parity (10 cinematic scene-builders + 3 phase-2 killers)
#   - ambito 3: glow (acceptance + temporal + determinism)
#   - ambito 4: motion blur (TICKET-MOTION-BLUR-TEXT scene)
#
# Aggregated by tests/text_golden_tests.cmake.
# ═══════════════════════════════════════════════════════════════════════════

# ADR-015 (TICKET-AE-PARITY-SUITE) — 5 cinematic AE-parity scene-builders.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_01_cinematic_title_reveal.cpp
        text_golden/ae_parity/ae_02_typewriter.cpp
        text_golden/ae_parity/ae_03_word_cascade.cpp
        text_golden/ae_parity/ae_04_fill_stroke_shadow.cpp
        text_golden/ae_parity/ae_05_lower_third.cpp
)

# TICKET-AE-PARITY-CINEMATIC-09/06/07/11/08/10/12/14 — extra ae_parity scenes.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_09_blur_in.cpp
        text_golden/ae_parity/ae_06_tracking_expansion.cpp
        text_golden/ae_parity/ae_07_stroke_reveal.cpp
        text_golden/ae_parity/ae_11_rotation_per_character.cpp
        text_golden/ae_parity/ae_08_glow_pulse.cpp
        text_golden/ae_parity/ae_10_scale_pop.cpp
        text_golden/ae_parity/ae_12_random_character_jitter.cpp
        text_golden/ae_parity/ae_14_multiline_9_16.cpp
        visual/ae_parity/ae_glow_smoke.cpp
)

# TICKET-AE-PARITY-KILLER-* — phase-2 killer tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp
        text_golden/ae_parity_killer/killer_02_expression_selector.cpp
        text_golden/ae_parity_killer/killer_03_character_offset.cpp
)

# TICKET-GLOW-CERTIFICATION — Azione 1/2/3: glow acceptance + temporal + determinism.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        visual/glow_ab/glow_ab_acceptance.cpp
        visual/glow_ab/glow_temporal_tests.cpp
        visual/glow_ab/glow_determinism_tests.cpp
)

# TICKET-CHRONON-GLOW-FINAL — Phase 2 smoke ctest alias.
add_test(
    NAME TextGlowSmoke
    COMMAND chronon3d_text_golden_tests --test-case="PHASE-2: ae_08 cinematic additive glow preserves source *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME GlowAcceptance
    COMMAND chronon3d_text_golden_tests --test-case="Glow acceptance: *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME GlowTemporal
    COMMAND chronon3d_text_golden_tests --test-case="Glow temporal: *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME GlowDeterminism
    COMMAND chronon3d_text_golden_tests --test-case="Glow determinism: *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-VIDEO-ANTI-FLICKER §8 + TICKET-VIDEO-MULTI-FPS-EQUIVALENCE §13
# (Video Completeness Matrix — bundled with the cinematic cluster because
# it depends on the AE parity motion-blur and glow composition primitives).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text/test_video_flicker_fps.cpp
)

# TICKET-MOTION-BLUR-TEXT — motion-blur text smoke goldens.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/motion_blur_text/motion_blur_text_scene.cpp
)

# TICKET-AE-PARITY-KILLER ctest filter.
add_test(
    NAME TextGoldenKiller
    COMMAND chronon3d_text_golden_tests --test-case="KILLER *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
