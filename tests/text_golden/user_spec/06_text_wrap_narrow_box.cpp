// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/06_text_wrap_narrow_box.cpp
//
// ADR-014 Decision 1 — Test 6: golden_text_wrap_narrow_box.
// 500×500 box (narrow), long sentence, word wrap, no cut words.
// Golden target: user_spec_06_text_wrap_narrow_box.png
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

GoldenTestConfig make_test06_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_06";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test06_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/06/text_wrap_narrow_box",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [](LayerBuilder& l) {
                l.text("t", {
                    .content = {.value = "This is a long sentence that must wrap into multiple lines without cutting words."},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {200.0f, 100.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 36.0f},
                    .layout = {.box = {500.0f, 500.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Top,
                               .wrap = TextWrap::Word},
                    .appearance = {.color = Color::white()},
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 06: text wrap narrow box — 500x500") {
    auto renderer = test::make_renderer();
    auto comp = build_test06_composition(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "user_spec_06_text_wrap_narrow_box", make_test06_config());
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}
