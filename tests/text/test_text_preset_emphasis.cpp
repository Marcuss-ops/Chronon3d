// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_preset_emphasis.cpp — Phase-2.1 P0 split
//
// 4 Emphasis-tier presets × 8 sentinels (4 timestamps × 2 aspect ratios).
// Mechanical split-off from the monolithic
// tests/text/test_text_preset_visual.cpp.
//
//   word_pop · scale_punch · color_accent · gradient_fill
//
// Shared header discipline: this file pulls in ONLY the fixture.hpp
// (composition build + render + metric + gate; the metric POD + scan
// are #include'd transitively inside the fixture) + sentinels.hpp
// (pure-data 8 sentinels × 4 = 32 uint64_t literals).
// ═══════════════════════════════════════════════════════════════════════════

#include <tests/text/visual/text_visual_fixture.hpp>
#include <tests/text/visual/text_visual_sentinels.hpp>

TEST_CASE("VRTextPreset/WordPop") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 0,  kRefTextPresWordPop_169_F000, "WordPop_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 20, kRefTextPresWordPop_169_F020, "WordPop_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 30, kRefTextPresWordPop_169_F030, "WordPop_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 40, kRefTextPresWordPop_169_F040, "WordPop_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 0,  kRefTextPresWordPop_916_F000, "WordPop_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 20, kRefTextPresWordPop_916_F020, "WordPop_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 30, kRefTextPresWordPop_916_F030, "WordPop_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 40, kRefTextPresWordPop_916_F040, "WordPop_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/ScalePunch") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 0,  kRefTextPresScalePunch_169_F000, "ScalePunch_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 20, kRefTextPresScalePunch_169_F020, "ScalePunch_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 30, kRefTextPresScalePunch_169_F030, "ScalePunch_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 40, kRefTextPresScalePunch_169_F040, "ScalePunch_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 0,  kRefTextPresScalePunch_916_F000, "ScalePunch_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 20, kRefTextPresScalePunch_916_F020, "ScalePunch_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 30, kRefTextPresScalePunch_916_F030, "ScalePunch_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 40, kRefTextPresScalePunch_916_F040, "ScalePunch_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/ColorAccent") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 0,  kRefTextPresColorAccent_169_F000, "ColorAccent_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 20, kRefTextPresColorAccent_169_F020, "ColorAccent_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 30, kRefTextPresColorAccent_169_F030, "ColorAccent_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 40, kRefTextPresColorAccent_169_F040, "ColorAccent_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 0,  kRefTextPresColorAccent_916_F000, "ColorAccent_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 20, kRefTextPresColorAccent_916_F020, "ColorAccent_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 30, kRefTextPresColorAccent_916_F030, "ColorAccent_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 40, kRefTextPresColorAccent_916_F040, "ColorAccent_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/GradientFill") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 0,  kRefTextPresGradientFill_169_F000, "GradientFill_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 20, kRefTextPresGradientFill_169_F020, "GradientFill_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 30, kRefTextPresGradientFill_169_F030, "GradientFill_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 40, kRefTextPresGradientFill_169_F040, "GradientFill_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 0,  kRefTextPresGradientFill_916_F000, "GradientFill_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 20, kRefTextPresGradientFill_916_F020, "GradientFill_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 30, kRefTextPresGradientFill_916_F030, "GradientFill_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 40, kRefTextPresGradientFill_916_F040, "GradientFill_916_F040", VisualExpectation::Visible);
}
