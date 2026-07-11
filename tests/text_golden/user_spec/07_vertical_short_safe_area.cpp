// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/07_vertical_short_safe_area.cpp
//
// ADR-014 Decision 1 — Test 7: golden_vertical_short_safe_area.
// 1080×1920 (9:16) canvas, social-vertical safe area.
// "BREAKING NEWS" big title top, "Chronon3D render test" subtitle bottom.
// Verifies coordinate semantics in 9:16 + anchor safety + no clipping.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
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

GoldenTestConfig make_test07_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_07";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test07_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/07/vertical_short_safe_area",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            // Title: top safe area (y ~ 280 of 1920 = top 14%)
            s.layer("title", [](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "BREAKING NEWS"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 128.0f},
                    .layout = {.box = {920.0f, 200.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {540.0f, 280.0f, 0.0f}
                });
            });
            // Subtitle: bottom safe area (y ~ 1640 of 1920 = bottom 15%)
            s.layer("subtitle", [](LayerBuilder& l) {
                l.text("subtitle", {
                    .content = {.value = "Chronon3D render test"},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 56.0f},
                    .layout = {.box = {920.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.85f, 0.85f, 0.85f, 1.0f}},
                    .position = {540.0f, 1640.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 07: 9:16 vertical safe area 1080x1920") {
    auto renderer = test::make_renderer();
    auto comp = build_test07_composition(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);

    auto result = verify_golden(*fb, "user_spec_07_vertical_short_safe_area", make_test07_config());
    REQUIRE_GOLDEN_PASSED(result);
}
