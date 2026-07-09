# ═══════════════════════════════════════════════════════════════════════════
# tests/text_golden_tests.cmake — TXT-QA-01 Real Golden Text Harness
#
# Standalone executable for the text golden regression test suite.
# Kept separate from chronon3d_text_preset_visual_tests to avoid
# unity-build conflicts between golden_test.cpp / image_diff.cpp
# and the text visual harness includes.
# Migrated to chronon3d_add_test_suite(TIER INTEGRATION) (this commit,
# closing the §11.1 migration backlog).
#
# Golden PNGs:   test_renders/golden/text/
# Artifacts:     test_renders/artifacts/text/
#
# Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGolden

chronon3d_add_test_suite(
    NAME chronon3d_text_golden_tests
    TIER INTEGRATION
    # TICKET-RENDER-PIPELINE-INTEGRITY M2 fix: chronon3d_software is an
    # INTERFACE that does NOT transitively pull in chronon3d_core (and
    # therefore chronon3d_runtime / RenderRuntime).  The new whitespace
    # test materialises TextRunShape via runtime::RenderRuntime, so we
    # link chronon3d_runtime explicitly.  chronon3d_text_core provides
    # FontEngine.
    LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_content
                  chronon3d_runtime chronon3d_text_core
    SOURCES text/test_text_golden.cpp
            visual/support/golden_test.cpp
            visual/support/image_diff.cpp
            # TICKET-RENDER-PIPELINE-INTEGRITY layer 3 — whitespace guard
            # short-circuits before compile_text_layout.
            text/test_materialize_whitespace_guarded.cpp
)
target_compile_definitions(chronon3d_text_golden_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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

# ADR-015 (TICKET-AE-PARITY-SUITE) — 5 cinematic AE-parity scene-builders.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_01_cinematic_title_reveal.cpp
        text_golden/ae_parity/ae_02_typewriter.cpp
        text_golden/ae_parity/ae_03_word_cascade.cpp
        text_golden/ae_parity/ae_04_fill_stroke_shadow.cpp
        text_golden/ae_parity/ae_05_lower_third.cpp
)

# TICKET-AE-PARITY-CINEMATIC-09 — ae_blur_in scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_09_blur_in.cpp
)

# TICKET-AE-PARITY-CINEMATIC-06 — ae_tracking_expansion scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_06_tracking_expansion.cpp
)

# TICKET-AE-PARITY-CINEMATIC-07 — ae_stroke_reveal scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_07_stroke_reveal.cpp
)

# TICKET-AE-PARITY-CINEMATIC-11 — ae_rotation_per_character scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_11_rotation_per_character.cpp
)

# TICKET-AE-PARITY-CINEMATIC-08 — ae_glow_pulse scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_08_glow_pulse.cpp
)

# TICKET-AE-PARITY-CINEMATIC-10 — ae_scale_pop scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_10_scale_pop.cpp
)

# TICKET-AE-PARITY-CINEMATIC-12 — ae_random_character_jitter scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_12_random_character_jitter.cpp
)

# TICKET-AE-PARITY-CINEMATIC-14 — ae_multiline_9_16_safezone scene.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_14_multiline_9_16.cpp
)

# TICKET-MOTION-BLUR-TEXT — motion-blur text smoke goldens.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/motion_blur_text/motion_blur_text_scene.cpp
)

# TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION — Phase 2 Killer 1.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp
)

# TICKET-AE-PARITY-KILLER-EXPRESSION-SELECTOR — Phase 2 Killer 2.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity_killer/killer_02_expression_selector.cpp
)

# TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE — Phase 2 Killer 3.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity_killer/killer_03_character_offset.cpp
)

# ═══════════════════════════════════════════════════════════════════════════
# Text Placement Golden Suite — content registerable + golden test.
# Covers: dashboard (8), anti-double-translation, layout box, clipping (7),
# multi-resolution (4), cache invalidation.
# ═══════════════════════════════════════════════════════════════════════════
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_placement/text_placement_golden.cpp
)

# P0-1 Text Visible Ink — anti-false-positive regression tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_visible_ink.cpp
)

# P0-7 Text Typewriter — frame-by-frame kinetic typography tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_typewriter.cpp
)

add_test(
    NAME TextTypewriter
    COMMAND chronon3d_text_golden_tests --test-case="TextTypewriter *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P0-5/6 Text Unicode — Unicode scripts + font fallback tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_unicode.cpp
)

add_test(
    NAME TextUnicode
    COMMAND chronon3d_text_golden_tests --test-case="TextUnicode *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P0-4 Text Alignment — bbox position verification for all align combos.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_alignment.cpp
)

add_test(
    NAME TextAlign
    COMMAND chronon3d_text_golden_tests --test-case="TextAlign *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P0-2 Text No Clipping — glyph clipping regression tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_no_clipping.cpp
)

add_test(
    NAME TextNoClip
    COMMAND chronon3d_text_golden_tests --test-case="NoClip *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P0-3 Text Wrapping — layout box wrapping correctness tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_wrapping.cpp
)

# P1-9 Text Style Properties — atomic property-coverage tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_style_properties.cpp
)

add_test(
    NAME TextStyleProps
    COMMAND chronon3d_text_golden_tests --test-case="TextStyle *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P1-10 Text Determinism — render determinism regression tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_determinism.cpp
)

add_test(
    NAME TextDeterminism
    COMMAND chronon3d_text_golden_tests --test-case="TextDeterminism *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P2-11 Text Long Text — stress tests for long inputs.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_long_text.cpp
)

add_test(
    NAME TextLongText
    COMMAND chronon3d_text_golden_tests --test-case="TextLongText *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# P2-12 Text Edge Cases — pathological input resilience tests.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_edge_cases.cpp
)

add_test(
    NAME TextEdgeCases
    COMMAND chronon3d_text_golden_tests --test-case="TextEdge *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Filter ctest -R to the wrapping subset.
add_test(
    NAME TextWrapping
    COMMAND chronon3d_text_golden_tests --test-case="TextWrap *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Filter ctest -R to the visible-ink subset.
add_test(
    NAME TextVisibleInk
    COMMAND chronon3d_text_golden_tests --test-case="VisibleInk *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-TEXT-CLIP-ASCENT — text-clip numerical-bbox regression lock.
# Five TEST_CASEs in text_clip/text_clip_bounds.cpp:
#   - AscentNotCut    : baseline HAMBURGER, asserts visible_height>90,
#                       visible_width>500, no right-edge touch.
#   - RightEdgeNotCut : exclusive right-edge assertion.
#   - Scale130NotCut  : uniform scale 1.30x → bbox grows proportionally
#                       AND still stays inside the framebuffer.
#   - ShadowNotCut    : drop shadow offset {20,40} + blur 30 → shadow
#                       padding path exercised; glyph ink not clipped.
#   - GlowNotCut      : layer-level glow radius 24 intensity 0.8 →
#                       glow compositor path exercised; glyph not clipped.
# The numeric bbox scans fail IMMEDIATELY on the pre-fix code; the
# golden PNG diff is a belt+suspenders safety net once the fix lands.
# See docs/CHANGELOG.md TICKET-TEXT-CLIP-ASCENT entry for symptom
# context (visible_bbox: x=974..1919, y=783..801 — 19 px tall — on
# output/ae_08_glow_pulse.png).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_clip/text_clip_bounds.cpp
)

# TextCompleteness — comprehensive glyph-visibility regression tests.
# 18 TEST_CASEs: AscentDescent, TopNotCut, BottomNotCut, AdvanceWidthNotCut,
#                Scale130NotCut, GlowNotCut, ShadowNotCut, MultilineNotCut,
#                LeftOverhangNotCut, HugeFontNotCut, Scale200NotCut,
#                IntentionalOverflowClip, CacheFrameChanges, MultiFontNotCut,
#                DebugBoundsMatchAlpha, Scale200NotCutHugeFont,
#                IntentionalOverflowEllipsis, TriFontNotCut.
# Uses shared test_helpers.hpp for AlphaBBox/alpha_bbox/alpha_centroid.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_clip/text_completeness.cpp
)

# Filter ctest -R to the text-clip numerical-bbox subset (ADR-NNN).
add_test(
    NAME TextClipBounds
    COMMAND chronon3d_text_golden_tests --test-case="Clip *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TextCompleteness ctest alias.
add_test(
    NAME TextCompleteness
    COMMAND chronon3d_text_golden_tests --test-case="TextCompleteness*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ── add_test aliases (preserved verbatim — these are NOT registered via
# the chronon3d_add_test_suite helper because they filter on doctest
# --test-case patterns, not on the executable itself). ───────────────
add_test(
    NAME TextGolden
    COMMAND chronon3d_text_golden_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
# Filter ctest -R to the user-spec subset only (per ADR-014 Decision 3).
add_test(
    NAME TextGoldenUserSpec
    COMMAND chronon3d_text_golden_tests --test-case="UserSpec*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
# TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION — KILLER* ctest filter.
add_test(
    NAME TextGoldenKiller
    COMMAND chronon3d_text_golden_tests --test-case="KILLER *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ── Text Placement Golden Suite ────────────────────────────────────────
add_test(
    NAME TextPlacement
    COMMAND chronon3d_text_golden_tests --test-case="TextPlace *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
