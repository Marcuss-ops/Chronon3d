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

# TICKET-AE-PARITY-CINEMATIC-08 — ae_glow_pulse scene + Phase 2 smoke.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/ae_parity/ae_08_glow_pulse.cpp
        visual/ae_parity/ae_glow_smoke.cpp
)

# TICKET-CHRONON-GLOW-FINAL — Phase 2 smoke ctest alias.
# 2 TEST_CASEs: 16:9 + 9:16 cinemat­ic-glow luminance preservation
# (glow_luma >= 0.98 * source_luma at the alpha centroid).
add_test(
    NAME TextGlowSmoke
    COMMAND chronon3d_text_golden_tests --test-case="PHASE-2: ae_08 cinematic additive glow preserves source *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
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

# Anti-False-Green — negative tests that MUST fail on bogus output.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_anti_false_green.cpp
)

add_test(
    NAME TextAntiFalseGreen
    COMMAND chronon3d_text_golden_tests --test-case="AntiFalseGreen *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Golden coverage gaps — Hebrew RTL, text gradient fill, camera DOF.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_completeness/text_golden_gaps.cpp
)

add_test(
    NAME TextGoldenGaps
    COMMAND chronon3d_text_golden_tests --test-case="GoldenGap *"
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
    COMMAND chronon3d_text_golden_tests --test-case="TextCompleteness.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — First test of the V0.2
# transforms/animation cluster.  Verifies that text rotated around the
# Z axis (in the canvas plane) does NOT clip at the canvas edges.
# 6 PNG goldens in `test_renders/golden/text/text_transforms_animation/rotate_z_not_cut/`
# (3 rotations × 2 ARs: 15°/45°/90° × 1920×1080/1080×1920).  Graceful
# skip on `result.golden_missing` per the §13 honest-gap pattern.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 ctest alias.
add_test(
    NAME TextRotateZ
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.RotateZ *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Scale transform tests.
# 4 TEST_CASEs (uniform 0.5× / 1.5× / 2.0× + non-uniform 0.96×1.04) ×
# 1 AR (1920×1080) = 4 PNG goldens in
# `test_renders/golden/text/text_transforms_animation/scale/`.
# Invariants: non-empty alpha_bbox + centroid near canvas center
# (anchored) + bbox dimensions grow monotonically with scale.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/02_scale.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Anchor transform tests.
# 4 TEST_CASEs (anchor TopLeft / TopRight / BottomLeft / BottomRight) ×
# 1 AR (1920×1080) = 4 PNG goldens in
# `test_renders/golden/text/text_transforms_animation/anchor/`.
# Invariants: non-empty alpha_bbox + centroid in expected quadrant.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/03_anchor.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Parent transform tests.
# 2 TEST_CASEs (parent at +500 X / parent at -300 X) × 1 AR (1920×1080)
# = 2 PNG goldens in
# `test_renders/golden/text/text_transforms_animation/parent_transform/`.
# Invariants: non-empty alpha_bbox + centroid X offset by parent
# position (both + and - offsets) + centroid Y near canvas center.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/04_parent_transform.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Extended rotation angle tests
# (negative + zero, complementing 01_rotate_z_not_cut.cpp's +15°..+90°).
# 4 TEST_CASEs (rotation -45° / -30° / -15° / 0°) × 1 AR (1920×1080) =
# 4 PNG goldens in
# `test_renders/golden/text/text_transforms_animation/rotation_extended/`.
# Invariants: non-empty alpha_bbox + centroid near canvas center.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/05_rotation_extended.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — 2.5D camera / 3D-enabled text.
# 1 TEST_CASE × 1 AR (1920×1080) = 1 PNG golden in
# `test_renders/golden/text/text_transforms_animation/two_point_five_d/`.
# Invariants: non-empty alpha_bbox + centroid near canvas center
# (depth doesn't translate X/Y) + bbox not collapsed to 0 by
# perspective.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/06_2_5d_camera.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 ctest aliases for the
# transforms subset.
add_test(
    NAME TextTransformsScale
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.Scale_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextTransformsAnchor
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.Anchor_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextTransformsParent
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.ParentTransform_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextTransformsRotationExt
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.RotateZ_m*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextTransforms2_5D
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.TwoPointFiveD_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Position animation tests.
# 3 TEST_CASEs (frame 0 / 15 / 30) × 1 AR (1920×1080) = 3 PNG goldens
# in
# `test_renders/golden/text/text_transforms_animation/anim_position/`.
# Frame-by-frame invariants: non-empty alpha_bbox at every frame +
# centroid X position INCREASES across frames (linear X translation) +
# centroid Y stays roughly constant (X-only animation).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/anim_01_position.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Opacity animation tests.
# 3 TEST_CASEs (frame 0 / 15 / 30) × 1 AR (1920×1080) = 3 PNG goldens
# in
# `test_renders/golden/text/text_transforms_animation/anim_opacity/`.
# Frame-by-frame invariants: non-empty alpha_bbox + max_alpha CHANGES
# monotonically (1.0 → 0.55 → 0.1).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/anim_02_opacity.cpp
)

# TICKET-FASE2-TRANSFORMS-ANIMATION §10 ctest aliases for the
# animations subset (first batch: position + opacity).
add_test(
    NAME TextAnimPosition
    COMMAND chronon3d_text_golden_tests --test-case="TextAnim.Position_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextAnimOpacity
    COMMAND chronon3d_text_golden_tests --test-case="TextAnim.Opacity_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-FASE3-MULTILINGUAL §KerningPairs — first of the 3 genuinely new
# multilingual text goldens.  3 PNG goldens in
# `test_renders/golden/text/text_multilingual/kerning_pairs/`
# (hero 200pt / body 96pt / with-tracking).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/01_kerning_pairs.cpp
)

# TICKET-FASE3-MULTILINGUAL §MixedAdvanceWidths — second genuinely new
# multilingual text golden.  3 PNG goldens in
# `test_renders/golden/text/text_multilingual/mixed_advance_widths/`
# (Latin-only / CJK-only / mixed).  CJK depends on system font fallback
# (NotoSansCJK) per honest-gap documentation in the test file.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/02_mixed_advance_widths.cpp
)

# TICKET-FASE3-MULTILINGUAL §MixedBaseline — third genuinely new
# multilingual text golden.  3 PNG goldens in
# `test_renders/golden/text/text_multilingual/mixed_baseline/`
# (default / +20px subscript / -20px superscript).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/03_mixed_baseline.cpp
)

# TICKET-FASE3-MULTILINGUAL §HangulComposition — fourth genuinely new
# multilingual text golden.  3 TEST_CASEs × 2 ARs = 6 PNG goldens in
# `test_renders/golden/text/text_multilingual/hangul_composition/`
# (simple syllables / complex syllables / real Korean word).
# Exercises HarfBuzz Hangul syllable-decomposition shaping
# (onset + nucleus + coda).  Depends on NotoSansCJK fallback for
# Hangul glyphs — Inter-Bold does NOT contain Hangul natively.
# NOTE: source registration was missing from the original cycle 4
# commit (`5efcc301`); added in cycle 5 (this commit) to make the
# build complete.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/04_hangul_composition.cpp
)

# TICKET-FASE3-MULTILINGUAL §DevanagariConjuncts — fifth genuinely new
# multilingual text golden.  3 TEST_CASEs × 2 ARs = 6 PNG goldens in
# `test_renders/golden/text/text_multilingual/devanagari_conjuncts/`
# (simple conjuncts / conjuncts with vowel marks / real Devanagari words).
# Exercises HarfBuzz Devanagari shaping: virama/halant (U+094D) conjunct
# decomposition + pre-base/post-base vowel mark positioning. Depends on
# system font fallback (Noto Sans Devanagari on Linux, Kohinoor
# Devanagari on macOS, Mangal on Windows) — Inter-Bold does NOT contain
# Devanagari glyphs natively.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/05_devanagari_conjuncts.cpp
)

# TICKET-FASE3-MULTILINGUAL §ArabicShaping — sixth genuinely new
# multilingual text golden.  3 TEST_CASEs × 2 ARs = 6 PNG goldens in
# `test_renders/golden/text/text_multilingual/arabic_shaping/`
# (basic joining / lam-alef ligatures / diacritics).  Exercises
# HarfBuzz Arabic shaping: 4 positional forms (isolated/initial/medial/
# final) for connector + non-connector letters, 4 mandatory lam-alef
# ligatures (لا/لأ/لإ/لآ), and 7 of the 8 main combining diacritics
# (fatha/kasra/damma/sukun/shadda/fathatan/dammatan) + RTL base
# direction.  Depends on system font fallback (Noto Sans Arabic on
# Linux, Geeza Pro on macOS, Arial on Windows) — Inter-Bold does NOT
# contain Arabic glyphs natively.  RTL is auto-detected by HarfBuzz
# from the Arabic Unicode block; no explicit `TextDirection::RTL` is
# required by the current pipeline.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/06_arabic_shaping.cpp
)

# TICKET-FASE3-MULTILINGUAL §HebrewNikud — seventh genuinely new
# multilingual text golden.  3 TEST_CASEs × 2 ARs = 6 PNG goldens in
# `test_renders/golden/text/text_multilingual/hebrew_nikud/`
# (base consonants + 5 final letter forms / nikud vowels / nikud +
# final forms combined).  Exercises HarfBuzz Hebrew shaping: 5 final
# letter forms (כ→ך kaf, מ→ם mem, נ→ן nun, פ→ף pe, צ→ץ tsade), 6 of
# the 10 nikud types (qamats/patach/segol/chirik/cholam/dagesh), and
# the shin/sin dot (שׁ U+05C1) which disambiguates shin (sh) from sin
# (s) + RTL base direction.  Depends on system font fallback (Noto
# Serif Hebrew or Noto Sans Hebrew on Linux, New Peninim MT on macOS,
# David CLM on Windows) — Inter-Bold does NOT contain Hebrew glyphs
# natively.  RTL is auto-detected by HarfBuzz from the Hebrew Unicode
# block; no explicit `TextDirection::RTL` is required.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/07_hebrew_nikud.cpp
)

# TICKET-FASE3-MULTILINGUAL ctest aliases.
add_test(
    NAME TextMultilingualKerningPairs
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.KerningPairs *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualMixedAdvanceWidths
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.MixedAdvanceWidths *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualMixedBaseline
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.MixedBaseline *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualHangulComposition
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.HangulComposition *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualDevanagariConjuncts
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.DevanagariConjuncts *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualArabicShaping
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.ArabicShaping *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualHebrewNikud
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.HebrewNikud *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-FASE3-MULTILINGUAL §FallbackMatrix — 10-case multilingual +
# fallback golden matrix.  10 TEST_CASEs (ASCII, Latin accents, Arabic
# RTL, Hebrew RTL, CJK, emoji, punctuation, numbers, combining marks,
# ligatures) × 1 AR (1920×1080) = 10 PNG goldens in
# `test_renders/golden/text/text_multilingual/fallback_matrix/`.  Each
# test case asserts the conservative-bbox-fallback counter
# (`text_bbox_contract_violations` in `RenderCounters`) is 0 in the
# nominal case, which regression-locks the font fallback chain +
# bbox computation for the 10 representative text categories.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/08_fallback_matrix.cpp
)

add_test(
    NAME TextMultilingualFallbackMatrix
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.FallbackMatrix *"
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

# ── Text Export V1 — deterministic golden regression ────────────────────
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_export/text_export_golden.cpp
)

add_test(
    NAME TextExportGolden
    COMMAND chronon3d_text_golden_tests --test-case="TextExportGolden*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
