// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_golden.cpp — TXT-QA-01 Real Golden Text Harness
//
// Standalone golden test suite for the text pipeline.  Uses the
// golden_test.hpp framework directly (verify_golden) — does NOT
// depend on the text_visual_fixture.hpp harness (which has unity-build
// include-order sensitivities).
//
// Five certified scenarios:
//   1. static_fill_stroke    — static text with white fill + blue stroke
//   2. fade_in               — opacity reveal
//   3. blur_in               — focus-blur entrance
//   4. word_cascade          — per-word cascade entrance
//   5. cinematic_title_reveal — cinematic tier title reveal
//
// Test matrix (per scenario):
//   • Aspect ratio: 16:9 (1920×1080) + 9:16 (1080×1920)
//   • Timestamps:   F0 (0%), F20 (33%), F30 (50%), F40 (67%)
//   • Frame rate:   30 fps
//
// Golden PNGs: test_renders/golden/text/<short_label>.png
// Artifacts:    test_renders/artifacts/text/<short_label>/
//
// Update goldens:  CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGolden
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/registry/text_preset_registry.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>
#include <content/text/text_helpers.hpp>
#include <chronon3d/text/text_definition.hpp>  // F2.C — from_text_definition()

#include <chrono>
#include <filesystem>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

// ═══════════════════════════════════════════════════════════════════════════
// Shared helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

enum class AspectRatioBin : int { k16x9 = 0, k9x16 = 1 };

struct AspectDims { int width; int height; };

AspectDims aspect_dims(AspectRatioBin r) {
    return r == AspectRatioBin::k16x9 ? AspectDims{1920, 1080}
                                       : AspectDims{1080, 1920};
}

// Pre-built, frozen text preset registry (matching text_visual_fixture.hpp).
const registry::TextPresetRegistry& shared_text_preset_registry() {
    static const registry::TextPresetRegistry kReg = []() {
        auto reg = registry::make_default_text_preset_registry();
        reg.freeze();
        return reg;
    }();
    return kReg;
}

// Build a composition that applies the named preset.
// renderer: required so SceneBuilder can bind font_engine() for text layout.
Composition build_preset_composition(SoftwareRenderer& renderer,
                                      const std::string& preset_id,
                                      AspectRatioBin r,
                                      int fps = 30) {
    AspectDims d = aspect_dims(r);
    const auto& preset = shared_text_preset_registry().get(preset_id);

    return composition(
        {.name = "VR/Text/" + preset_id,
         .width = d.width, .height = d.height,
         .frame_rate = FrameRate{fps, 1},
         .duration = 60},
        [&renderer, preset, d](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const f32 font_size = (d.width >= d.height) ? 96.0f : 64.0f;
            auto base = content::text::centered_text(
                content::text::CenterTextOptions{
                    .text        = "THE QUICK BROWN FOX JUMPS",
                    .box         = Vec2{d.width * 0.85f, d.height * 0.85f},
                    .font_asset   = "assets/fonts/Poppins-Bold.ttf",
                    .font_family = "Poppins",
                    .font_weight = 700,
                    .font_size   = font_size,
                    .color       = Color::white(),
                });
            s.layer("hero", [&s, &preset, base](LayerBuilder& l) {
                if (preset.builder) {
                    preset.builder(s, l, from_text_definition(base));
                }
            });
            return s.build();
        });
}

// Build a static fill+stroke composition (no preset).
// renderer: required so SceneBuilder can bind font_engine() for text layout.
Composition build_static_fill_stroke_composition(SoftwareRenderer& renderer,
                                                   AspectRatioBin r,
                                                   int fps = 30) {
    AspectDims d = aspect_dims(r);
    return composition(
        {.name = "VR/Text/static_fill_stroke",
         .width = d.width, .height = d.height,
         .frame_rate = FrameRate{fps, 1},
         .duration = 60},
        [&renderer, d](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const f32 fs = (d.width >= d.height) ? 96.0f : 64.0f;
            s.layer("hero", [fs, d](LayerBuilder& l) {
                l.text("t", {
                    .content = {.value = "THE QUICK BROWN FOX"},
                    .font = {.font_path = "assets/fonts/Poppins-Bold.ttf",
                             .font_family = "Poppins",
                             .font_size = fs},
                    .layout = {.box = {d.width * 0.85f, d.height * 0.85f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white(),
                                   .paint = {.stroke_enabled = true,
                                              .stroke_color = Color{0.2f, 0.6f, 1.0f, 1.0f},
                                              .stroke_width = 3.0f}},
                    .placement = TextPlacement{
                        TextPlacementKind::Absolute,
                        {d.width * 0.5f, d.height * 0.5f}}
                });
            });
            return s.build();
        });
}

// Golden config for text tests.
GoldenTestConfig text_golden_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0 / 255.0;
    cfg.threshold.max_abs_error          = 40.0 / 255.0;
    cfg.threshold.max_changed_pixel_ratio = 0.05;
    cfg.threshold.max_rmse               = 6.0 / 255.0;
    cfg.threshold.min_ssim               = 0.92;
    return cfg;
}

// Render a composition, verify against golden, return the framebuffer.
void verify_text_golden(SoftwareRenderer& renderer,
                         Composition& comp,
                         Frame frame,
                         const std::string& short_label) {
    auto fb = renderer.render(comp, frame);
    REQUIRE(fb != nullptr);

    auto cfg = text_golden_config();
    auto result = verify_golden(*fb, short_label, cfg);
    INFO("Golden: ", result.message);
    CHECK(result.passed);
}

// Emit a golden gate for a preset at a specific (ratio, frame) point.
void emit_golden_gate(SoftwareRenderer& renderer,
                       const std::string& preset_id,
                       AspectRatioBin r,
                       int t_frame,
                       const std::string& short_label) {
    auto comp = build_preset_composition(renderer, preset_id, r, 30);
    verify_text_golden(renderer, comp, Frame{t_frame}, short_label);
}

// Emit a golden gate for the static fill+stroke composition.
void emit_static_golden(SoftwareRenderer& renderer,
                         AspectRatioBin r,
                         int t_frame,
                         const std::string& short_label) {
    auto comp = build_static_fill_stroke_composition(renderer, r, 30);
    verify_text_golden(renderer, comp, Frame{t_frame}, short_label);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Scenario 1 — Static text with white fill + blue stroke
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TXT-QA-01: static text fill+stroke 16:9") {
    auto renderer = test::make_renderer();
    emit_static_golden(renderer, AspectRatioBin::k16x9, 0,  "TXT_QA01_static_fill_stroke_169_F000");
    emit_static_golden(renderer, AspectRatioBin::k16x9, 20, "TXT_QA01_static_fill_stroke_169_F020");
    emit_static_golden(renderer, AspectRatioBin::k16x9, 30, "TXT_QA01_static_fill_stroke_169_F030");
    emit_static_golden(renderer, AspectRatioBin::k16x9, 40, "TXT_QA01_static_fill_stroke_169_F040");
}

TEST_CASE("TXT-QA-01: static text fill+stroke 9:16") {
    auto renderer = test::make_renderer();
    emit_static_golden(renderer, AspectRatioBin::k9x16, 0,  "TXT_QA01_static_fill_stroke_916_F000");
    emit_static_golden(renderer, AspectRatioBin::k9x16, 20, "TXT_QA01_static_fill_stroke_916_F020");
    emit_static_golden(renderer, AspectRatioBin::k9x16, 30, "TXT_QA01_static_fill_stroke_916_F030");
    emit_static_golden(renderer, AspectRatioBin::k9x16, 40, "TXT_QA01_static_fill_stroke_916_F040");
}

// ═══════════════════════════════════════════════════════════════════════════
// Scenario 2 — fade_in
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TXT-QA-01: fade_in 16:9") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k16x9, 0,  "TXT_QA01_fade_in_169_F000");
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k16x9, 20, "TXT_QA01_fade_in_169_F020");
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k16x9, 30, "TXT_QA01_fade_in_169_F030");
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k16x9, 40, "TXT_QA01_fade_in_169_F040");
}

TEST_CASE("TXT-QA-01: fade_in 9:16") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k9x16, 0,  "TXT_QA01_fade_in_916_F000");
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k9x16, 20, "TXT_QA01_fade_in_916_F020");
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k9x16, 30, "TXT_QA01_fade_in_916_F030");
    emit_golden_gate(renderer, "fade_in", AspectRatioBin::k9x16, 40, "TXT_QA01_fade_in_916_F040");
}

// ═══════════════════════════════════════════════════════════════════════════
// Scenario 3 — blur_in
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TXT-QA-01: blur_in 16:9") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k16x9, 0,  "TXT_QA01_blur_in_169_F000");
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k16x9, 20, "TXT_QA01_blur_in_169_F020");
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k16x9, 30, "TXT_QA01_blur_in_169_F030");
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k16x9, 40, "TXT_QA01_blur_in_169_F040");
}

TEST_CASE("TXT-QA-01: blur_in 9:16") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k9x16, 0,  "TXT_QA01_blur_in_916_F000");
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k9x16, 20, "TXT_QA01_blur_in_916_F020");
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k9x16, 30, "TXT_QA01_blur_in_916_F030");
    emit_golden_gate(renderer, "blur_in", AspectRatioBin::k9x16, 40, "TXT_QA01_blur_in_916_F040");
}

// ═══════════════════════════════════════════════════════════════════════════
// Scenario 4 — word_cascade
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TXT-QA-01: word_cascade 16:9") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k16x9, 0,  "TXT_QA01_word_cascade_169_F000");
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k16x9, 20, "TXT_QA01_word_cascade_169_F020");
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k16x9, 30, "TXT_QA01_word_cascade_169_F030");
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k16x9, 40, "TXT_QA01_word_cascade_169_F040");
}

TEST_CASE("TXT-QA-01: word_cascade 9:16") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k9x16, 0,  "TXT_QA01_word_cascade_916_F000");
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k9x16, 20, "TXT_QA01_word_cascade_916_F020");
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k9x16, 30, "TXT_QA01_word_cascade_916_F030");
    emit_golden_gate(renderer, "word_cascade", AspectRatioBin::k9x16, 40, "TXT_QA01_word_cascade_916_F040");
}

// ═══════════════════════════════════════════════════════════════════════════
// Scenario 5 — cinematic_title_reveal
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TXT-QA-01: cinematic_title_reveal 16:9") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k16x9, 0,  "TXT_QA01_cinematic_title_reveal_169_F000");
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k16x9, 20, "TXT_QA01_cinematic_title_reveal_169_F020");
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k16x9, 30, "TXT_QA01_cinematic_title_reveal_169_F030");
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k16x9, 40, "TXT_QA01_cinematic_title_reveal_169_F040");
}

TEST_CASE("TXT-QA-01: cinematic_title_reveal 9:16") {
    auto renderer = test::make_renderer();
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k9x16, 0,  "TXT_QA01_cinematic_title_reveal_916_F000");
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k9x16, 20, "TXT_QA01_cinematic_title_reveal_916_F020");
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k9x16, 30, "TXT_QA01_cinematic_title_reveal_916_F030");
    emit_golden_gate(renderer, "cinematic_title_reveal", AspectRatioBin::k9x16, 40, "TXT_QA01_cinematic_title_reveal_916_F040");
}
