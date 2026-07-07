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

# ADR-015 (TICKET-AE-PARITY-SUITE) — 5 cinematic AE-parity scene-builders.
# Cat-2 freeze-compliant (forward-only): zero new public API; verify_golden
# reuse from tests/visual/support/golden_test.hpp; same harness chain as the
# 12 user-spec tests above. 5 scene × 2 AR (16:9 + 9:16) × 3 frame (0,15,30)
# = 30 golden PNG target. Capture blocked by TICKET-GOLDEN-CAPTURE root
# cause until that ticket is closed (see FOLLOWUP_TICKETS.md); impl ships
# forward-only until capture pipeline is fixed.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_01_cinematic_title_reveal.cpp
        text_golden/ae_parity/ae_02_typewriter.cpp
        text_golden/ae_parity/ae_03_word_cascade.cpp
        text_golden/ae_parity/ae_04_fill_stroke_shadow.cpp
        text_golden/ae_parity/ae_05_lower_third.cpp
)

# TICKET-AE-PARITY-CINEMATIC-09 — ae_blur_in scene. Appended directly to the
# existing target via target_sources() (no new harness chain). 6 TEST_CASEs
# = 16:9 + 9:16 × 3 frame snapshots f00/f15/f30 with progressive blur tier
# (0.0f / 7.0f / 20.0f) locked against `kBlurTierRadii = {{0, 2, 7, 13, 20}}`
# (text_run_processor/text_run_stages.hpp:51). LayerBuilder::blur() → bucket
# via detail::bucket_radius_for_tier → apply_separable_box_blur. Parent-
# blocker TICKET-GOLDEN-CAPTURE closed 2026-07-06 (Phase F). Cat-2 freeze-
# compliant (zero new public API; verify_golden reuse; same harness chain).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_09_blur_in.cpp
)

# TICKET-AE-PARITY-CINEMATIC-06 — ae_tracking_expansion scene. Appended
# directly to existing target via target_sources(). 6 TEST_CASEs = 16:9 +
# 9:16 × 3 frame snapshots f00/f15/f30 with title-trailer-style reveal
# triple anim: tracking (.layout.tracking text-level field, values 4.0/12.0/
# 22.0), opacity (LayerBuilder::opacity, layer_builder.cpp:138, values
# 0.30/0.70/1.00) + blur reveal (LayerBuilder::blur → kBlurTierRadii tier
# 4/2/0). Cross-link ae_09_blur_in (already shipped); reused same blur
# mapping pattern. Cat-2 freeze-compliant.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_06_tracking_expansion.cpp
)

# TICKET-AE-PARITY-CINEMATIC-07 — ae_stroke_reveal scene. Appended directly
# to existing target via target_sources(). 6 TEST_CASEs = 16:9 + 9:16 × 3
# frame snapshots f00/f15/f30 with stroke-then-fill cinematic reveal:
# stroke paint always on (medial-edge silhouette scaffold stable across
# frames) while the inner fill Color α interpolates 0.05 → 0.55 → 1.00 —
# outline-only at f00 reading as pure stroke silhouette, half-outline /
# half-cream-tinted fill at f15, fully composited white-body + ink-stroke
# silhouette at f30. Stroke width stable at 8.0f for in-flight reveal,
# settles to 6.0f at f30 (typographic finalisation). Fill-color RGB
# interpolates amber→cream→white for cinematic poster-typography feel.
# Cross-link ae_04_fill_stroke_shadow (already shipped) — same .paint
# stroke_enabled + stroke_color + stroke_width pattern; ae_07 adds the
# fill-alpha choreography + RGB interpolation over the static stroke
# scaffold. Cat-2 freeze-compliant (zero new public API; verify_golden
# reuse; same harness chain).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_07_stroke_reveal.cpp
)

# TICKET-AE-PARITY-CINEMATIC-11 — ae_rotation_per_character scene. Appended
# directly to existing target via target_sources(). 6 TEST_CASEs = 16:9 +
# 9:16 × 3 frame snapshots f00/f15/f30 with Y-axis rotation progressive ramp
# (0° → 12° → 24° per-frame) + slight camera-prospettica Z-translation
# (z=0 → -60 → -120) to evoke depth kickoff for Phase 2 Killer 4
# (per-character 3D complete). Layer-level rotation approximation
# (whole text as a single rigid body rotating around its layer anchor);
# TRUE per-character fan composition via `.select(Character).rotation()`
# Animator chain is the forward-only Phase 2 Killer 4 ticket scope. Pure
# Y-axis here (Z rotation 0°, X rotation 0°) so the fan reads as a
# cinema-tilt-and-depth reveal rather than a full per-axis 3D rotation.
# LayerBuilder::rotate + LayerBuilder::position (Z-channel) — both
# LayerBuilder public surface, no new SDK surface. Cat-2 freeze-compliant.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_11_rotation_per_character.cpp
)

# TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION — Phase 2 Killer 1 determinism
# lock for the Wiggly/Wave/Random selector substrate. Forward-only note:
# `WigglySelector` and `WaveSelector` are PLANNED, NOT YET IMPL (zero
# `*wiggly*` / `*wave*` files in include/chronon3d/ + src/text/). This test
# locks the deterministic, thread-safe substrate that they will compose on:
#   * Cell-level (TEST_CASE 1, 3 SUBCASEs): hash_to_unit_float + Fisher-Yates
#     cached permutation get_or_build_permutation. Proves same-seed byte-
#     identity; different-seed divergence; parallel-compute race-freedom.
#   * End-to-end (TEST_CASE 2, 3 SUBCASEs): evaluate_animator with
#     TextSelectorOrder::Random + mocked PlacedGlyphRun + PositionProperty.
#     Proves same-seed per-glyph state byte-identity; different-seed
#     observable drift; cross-run reference lock (FNV-1a fingerprint).
# When TICKET-WIGGLY-IMPL + TICKET-WAVE-IMPL land, TEST_CASE 2 expands to
# include a 4th SUBCASE exercising the new Wiggly/Wave surfaces. The
# cell-level substrate (TEST_CASE 1) is the foundation they compose on.
# Cat-2 freeze-compliant (zero new public API surface; test-only includes).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp
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
# TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION — KILLER* ctest filter
# (CI isolation for the Phase 2 killer tests as a unit). Companion to
# `TextGolden` (all-tests) + `TextGoldenUserSpec` (12 user-spec only);
# this third filter runs only the KILLER-NN tests so CI can verify
# selector determinism in isolation from the visual golden suite.
add_test(
    NAME TextGoldenKiller
    COMMAND chronon3d_text_golden_tests --test-case="KILLER *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
