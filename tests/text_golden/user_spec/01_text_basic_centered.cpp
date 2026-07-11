// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/01_text_basic_centered.cpp
//
// ADR-014 Decision 1 — Test 1: golden_text_basic_centered.
// Pipeline smoke: TextDocument → TextRunLayout → TextRunShape → draw_text_run.
// 1920×1080 canvas, Inter Bold 96px, white, centered.
// Golden target: test_renders/golden/text/user_spec_01_text_basic_centered.png
//
// Pass criteria: PNG non vuoto, bounding box del testo vicino al centro canvas.
// Update mode: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGoldenUserSpec
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

GoldenTestConfig make_test01_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_01";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test01_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/01/text_basic_centered",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("title", TextRunSpec{
                    .text = TextSpec{.content = {.value = "Chronon3D Text Engine"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 96.0f}, .layout = {.box = {1920.0f, 1080.0f},
                                   .align = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle}, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 01: text basic centered 1920x1080 Inter Bold 96") {
    auto renderer = test::make_renderer();
    auto comp = build_test01_composition(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    auto result = verify_golden(*fb, "user_spec_01_text_basic_centered", make_test01_config());
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}
