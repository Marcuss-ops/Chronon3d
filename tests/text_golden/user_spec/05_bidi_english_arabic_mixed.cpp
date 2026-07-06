// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/05_bidi_english_arabic_mixed.cpp
//
// ADR-014 Decision 1 — Test 5: golden_bidi_english_arabic_mixed.
// "Hello سلام World" — FriBidi correctness (LTR + RTL + LTR run count).
// Golden target: user_spec_05_bidi_english_arabic_mixed.png
//
// FriBidi is REQUIRED per TICKET P1-2 gate (compile-time static_assert at
// tests/text/test_text_font_determinism.cpp:101). If FriBidi is missing,
// the build itself fails — no fallback LTR-only.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

GoldenTestConfig make_test05_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_05";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test05_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/05/bidi_english_arabic_mixed",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [](LayerBuilder& l) {
                l.text("t", {
                    // U+0633 U+0644 U+0627 U+0645 → "سلام" (Arabic for "peace")
                    .content = {.value = "Hello \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 World"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 05: bidi English+Arabic mixed — 1920x1080 FriBidi REQUIRED") {
    auto renderer = test::make_renderer();
    auto comp = build_test05_composition(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "user_spec_05_bidi_english_arabic_mixed", make_test05_config());
    INFO("Golden: ", result.message);
    if (result.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(result.passed);
}
