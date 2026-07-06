// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/10_text_fill_stroke.cpp
//
// ADR-014 Decision 1 — Test 10: golden_text_fill_stroke.
// "STROKE" — white fill + thick black stroke, 1920×1080.
// Verifies paint order (fill then stroke) + outline width + clean edges.
// Stroke must not destroy the fill; expected visible: white interior,
// black outline.
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

GoldenTestConfig make_test10_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_10";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test10_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/10/text_fill_stroke",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [](LayerBuilder& l) {
                l.text("stroke", {
                    .content = {.value = "STROKE"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 280.0f},
                    .layout = {.box = {1500.0f, 400.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {
                        .color = Color::white(),
                        .paint = {
                            .stroke_enabled = true,
                            .stroke_color = Color::black(),
                            .stroke_width = 8.0f
                        }
                    },
                    .position = {960.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 10: text fill + stroke 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test10_composition(renderer), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_10_text_fill_stroke", make_test10_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
