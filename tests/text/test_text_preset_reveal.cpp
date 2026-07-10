// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_preset_reveal.cpp — Phase-2.1 P0 split
//
// 10 Reveal-tier presets × 8 sentinels (4 timestamps × 2 aspect ratios).
// Mechanical split-off of TEST_CASEs that formerly lived in
// tests/text/test_text_preset_visual.cpp alongside the Emphasis and  // drift-allow: stale-ref
// Subtitle tiers.
//
//   text_animations (10) · fade_in · blur_in · slide_up · slide_down
//   scale_in · tracking_close · masked_line_reveal · word_cascade
//   character_cascade
//
// Per-TEST_CASE comment on tracking_close / word_cascade / character_cascade:
// AGENT 2 / TICKET-A2 — these 3 presets are now resolver-driven
// (animated entrances blur the static text on early-frames).  The
// Visible-frames hash shifts; old captured sentinels reset to
// kUncapturedSentinel so the visual test runs in capture mode
// ("first hash to capture: <hash>" message) until the next
// visual-rebaseline PR captures new goldens.
//
// Shared header discipline: the .hpp pulls in the metrics + expectations
// via bottom `#include`s, so this file only needs to include the
// fixture.hpp (line 1 below) + the data-only sentinels.hpp (line 2).
// ═══════════════════════════════════════════════════════════════════════════

#include <tests/text/visual/text_visual_fixture.hpp>
#include <tests/text/visual/text_visual_sentinels.hpp>

// Reveal (10 presets).
TEST_CASE("VRTextPreset/TextAnimations") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 0,  kRefTextPresTextAnimations_169_F000, "TextAnimations_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 20, kRefTextPresTextAnimations_169_F020, "TextAnimations_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 30, kRefTextPresTextAnimations_169_F030, "TextAnimations_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 40, kRefTextPresTextAnimations_169_F040, "TextAnimations_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 0,  kRefTextPresTextAnimations_916_F000, "TextAnimations_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 20, kRefTextPresTextAnimations_916_F020, "TextAnimations_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 30, kRefTextPresTextAnimations_916_F030, "TextAnimations_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 40, kRefTextPresTextAnimations_916_F040, "TextAnimations_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/FadeIn") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 0,  kRefTextPresFadeIn_169_F000, "FadeIn_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 20, kRefTextPresFadeIn_169_F020, "FadeIn_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 30, kRefTextPresFadeIn_169_F030, "FadeIn_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 40, kRefTextPresFadeIn_169_F040, "FadeIn_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 0,  kRefTextPresFadeIn_916_F000, "FadeIn_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 20, kRefTextPresFadeIn_916_F020, "FadeIn_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 30, kRefTextPresFadeIn_916_F030, "FadeIn_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 40, kRefTextPresFadeIn_916_F040, "FadeIn_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/BlurIn") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 0,  kRefTextPresBlurIn_169_F000, "BlurIn_169_F000", VisualExpectation::Transparent);
    // F020: focus_in(Frame{30}) at frame 20 = blur ≈ 1.08 (OutCubic 67%). Heavy mid-animation blur dilutes per-pixel alpha below the 0.05 ink threshold; not a regression.
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 20, kRefTextPresBlurIn_169_F020, "BlurIn_169_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 30, kRefTextPresBlurIn_169_F030, "BlurIn_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 40, kRefTextPresBlurIn_169_F040, "BlurIn_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 0,  kRefTextPresBlurIn_916_F000, "BlurIn_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 20, kRefTextPresBlurIn_916_F020, "BlurIn_916_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 30, kRefTextPresBlurIn_916_F030, "BlurIn_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 40, kRefTextPresBlurIn_916_F040, "BlurIn_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/SlideUp") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 0,  kRefTextPresSlideUp_169_F000, "SlideUp_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 20, kRefTextPresSlideUp_169_F020, "SlideUp_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 30, kRefTextPresSlideUp_169_F030, "SlideUp_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 40, kRefTextPresSlideUp_169_F040, "SlideUp_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 0,  kRefTextPresSlideUp_916_F000, "SlideUp_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 20, kRefTextPresSlideUp_916_F020, "SlideUp_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 30, kRefTextPresSlideUp_916_F030, "SlideUp_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 40, kRefTextPresSlideUp_916_F040, "SlideUp_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/SlideDown") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 0,  kRefTextPresSlideDown_169_F000, "SlideDown_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 20, kRefTextPresSlideDown_169_F020, "SlideDown_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 30, kRefTextPresSlideDown_169_F030, "SlideDown_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 40, kRefTextPresSlideDown_169_F040, "SlideDown_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 0,  kRefTextPresSlideDown_916_F000, "SlideDown_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 20, kRefTextPresSlideDown_916_F020, "SlideDown_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 30, kRefTextPresSlideDown_916_F030, "SlideDown_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 40, kRefTextPresSlideDown_916_F040, "SlideDown_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/ScaleIn") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 0,  kRefTextPresScaleIn_169_F000, "ScaleIn_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 20, kRefTextPresScaleIn_169_F020, "ScaleIn_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 30, kRefTextPresScaleIn_169_F030, "ScaleIn_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 40, kRefTextPresScaleIn_169_F040, "ScaleIn_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 0,  kRefTextPresScaleIn_916_F000, "ScaleIn_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 20, kRefTextPresScaleIn_916_F020, "ScaleIn_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 30, kRefTextPresScaleIn_916_F030, "ScaleIn_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 40, kRefTextPresScaleIn_916_F040, "ScaleIn_916_F040", VisualExpectation::Visible);
}

// AGENT 2 (TICKET-A2) — tracking_close IS now entrance-animated
// (AnimatedValue opacity 0→1 over 35 frames at Linear easing — the
// pre-refactor static `OpacityProperty{1.0f}` + layer-level
// tracking_breathing has been replaced).  The Visible-frames shape:
//   F000 → Transparent (opacity 0 → empty framebuffer)
//   F020 → PartialCoverage (opacity ≈ 0.57 → ink below VisibleMinPixels)
//   F030 → Visible (opacity ≈ 0.86 → ink above VisibleMinPixels, tracking cropped)
//   F040 → Visible (opacity 1 → end-state)
TEST_CASE("VRTextPreset/TrackingClose") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 0,  kRefTextPresTrackingClose_169_F000, "TrackingClose_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 20, kRefTextPresTrackingClose_169_F020, "TrackingClose_169_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 30, kRefTextPresTrackingClose_169_F030, "TrackingClose_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 40, kRefTextPresTrackingClose_169_F040, "TrackingClose_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 0,  kRefTextPresTrackingClose_916_F000, "TrackingClose_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 20, kRefTextPresTrackingClose_916_F020, "TrackingClose_916_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 30, kRefTextPresTrackingClose_916_F030, "TrackingClose_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 40, kRefTextPresTrackingClose_916_F040, "TrackingClose_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/MaskedLineReveal") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 0,  kRefTextPresMaskedLineReveal_169_F000, "MaskedLineReveal_169_F000", VisualExpectation::Transparent);
    // F020: center_split(Frame{30}) at frame 20 = scale_y ≈ 0.66, fade_shift_horizontal ≈ 0.94 opacity. Vertically compressed, mid-opacity text falls below the 0.05 ink threshold; not a regression.
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 20, kRefTextPresMaskedLineReveal_169_F020, "MaskedLineReveal_169_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 30, kRefTextPresMaskedLineReveal_169_F030, "MaskedLineReveal_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 40, kRefTextPresMaskedLineReveal_169_F040, "MaskedLineReveal_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 0,  kRefTextPresMaskedLineReveal_916_F000, "MaskedLineReveal_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 20, kRefTextPresMaskedLineReveal_916_F020, "MaskedLineReveal_916_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 30, kRefTextPresMaskedLineReveal_916_F030, "MaskedLineReveal_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 40, kRefTextPresMaskedLineReveal_916_F040, "MaskedLineReveal_916_F040", VisualExpectation::Visible);
}

// AGENT 2 (TICKET-A2) — word_cascade is now resolver-driven (Word-unit
// selector with animated `end` sweeping 0→100 over 48f; OpacityProperty/
// Position/Scale ramp 0→end_state over 36f).  All 8 sentinels reset so
// existing captured framebuffer hashes (old layer chain + fade_in) do
// not engage.
// F020: 5-word cascade at 3-frame stagger, 36-frame-per-word opacity
// ramp. Avg opacity ≈ 0.39 → diluted alpha falls below the 0.05 ink
// threshold for many glyphs. Mid-animation, not a regression.
TEST_CASE("VRTextPreset/WordCascade") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 0,  kRefTextPresWordCascade_169_F000, "WordCascade_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 20, kRefTextPresWordCascade_169_F020, "WordCascade_169_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 30, kRefTextPresWordCascade_169_F030, "WordCascade_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 40, kRefTextPresWordCascade_169_F040, "WordCascade_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 0,  kRefTextPresWordCascade_916_F000, "WordCascade_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 20, kRefTextPresWordCascade_916_F020, "WordCascade_916_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 30, kRefTextPresWordCascade_916_F030, "WordCascade_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 40, kRefTextPresWordCascade_916_F040, "WordCascade_916_F040", VisualExpectation::Visible);
}

// AGENT 2 (TICKET-A2) — character_cascade is now resolver-driven
// (Glyph-unit selector with animated `end` sweeping 0→100 over 24f;
// OpacityProperty/Position/Scale ramp 0→end_state over 18f).  All 8
// sentinels reset since the rendering now differs frame-by-frame.
// F020: glyph cascade at 1-frame stagger, 18-frame-per-glyph opacity
// ramp. By F020 all glyphs are post-ramp, but per-glyph positional
// offset still compresses alpha below the 0.05 ink threshold. Mid-
// animation, not a regression.
TEST_CASE("VRTextPreset/CharacterCascade") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 0,  kRefTextPresCharacterCascade_169_F000, "CharacterCascade_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 20, kRefTextPresCharacterCascade_169_F020, "CharacterCascade_169_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 30, kRefTextPresCharacterCascade_169_F030, "CharacterCascade_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 40, kRefTextPresCharacterCascade_169_F040, "CharacterCascade_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 0,  kRefTextPresCharacterCascade_916_F000, "CharacterCascade_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 20, kRefTextPresCharacterCascade_916_F020, "CharacterCascade_916_F020", VisualExpectation::PartialCoverage);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 30, kRefTextPresCharacterCascade_916_F030, "CharacterCascade_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 40, kRefTextPresCharacterCascade_916_F040, "CharacterCascade_916_F040", VisualExpectation::Visible);
}
