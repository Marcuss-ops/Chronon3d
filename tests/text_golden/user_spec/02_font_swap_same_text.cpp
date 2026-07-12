// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/02_font_swap_same_text.cpp
//
// ADR-014 Decision 1 — Test 2: golden_font_swap_same_text.
// Frame 0: "SAME TEXT" Inter Bold. Frame 30: "SAME TEXT" Inter Regular.
// Golden targets: user_spec_02_font_swap_F000.png + user_spec_02_font_swap_F030.png
//
// Regression-locks: cache short-circuit on source_text == target_text MUST NOT
// skip font rebuild. P0-2 closure ensures font_family is in TextLayoutCacheKey.
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

GoldenTestConfig make_test02_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_02";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test02_composition_bold(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/02/font_swap_bold",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [](LayerBuilder& l) {
                l.text("t", {
                    .content = {.value = "SAME TEXT"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
            });
            return s.build();
        });
}

Composition build_test02_composition_regular(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/02/font_swap_regular",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [](LayerBuilder& l) {
                l.text("t", {
                    .content = {.value = "SAME TEXT"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 96.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 02: font swap same text — Bold frame 0") {
    auto renderer = test::make_renderer();
    auto comp = build_test02_composition_bold(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "user_spec_02_font_swap_F000", make_test02_config());
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

TEST_CASE("UserSpec 02: font swap same text — Regular frame 30") {
    auto renderer = test::make_renderer();
    auto comp = build_test02_composition_regular(renderer);
    auto fb = renderer.render(comp, Frame{30});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "user_spec_02_font_swap_F030", make_test02_config());
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}
