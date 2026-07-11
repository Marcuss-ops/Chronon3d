// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_01_cinematic_title_reveal.cpp
//
// TICKET-AE-PARITY-SUITE — Scene 01: cinematic_title_reveal.
// "EPIC TITLE" hero text that scales up + fades in across 30 frames.
// Frame snapshots: 0 (low opacity, small), 15 (mid), 30 (full intensity, max scale).
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API; reuses
// verify_golden helper + composition() + SceneBuilder::layer() pattern.
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

GoldenTestConfig make_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_01_cinematic";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 6.0f / 255.0f;
    cfg.threshold.max_abs_error            = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.20f;
    cfg.threshold.max_rmse                 = 8.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.85f;
    return cfg;
}

// Cinematic reveal: scale-up + fade-in across 30 frames.
// Frame 0:  small (64px), 30% opacity → barely visible
// Frame 15: medium (96px), 70% opacity → mid-reveal
// Frame 30: full (140px), 100% opacity → climax
static float lerp_scale(std::size_t f) {
    if (f == 0)  return 64.0f;
    if (f <= 15) return 96.0f;
    return 140.0f;
}
static float lerp_alpha(std::size_t f) {
    if (f == 0)  return 0.30f;
    if (f <= 15) return 0.70f;
    return 1.00f;
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/01/cinematic_title_reveal/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float scale = lerp_scale(frame_idx);
                const float alpha = lerp_alpha(frame_idx);
                l.text("title", {
                    .content = {.value = "EPIC TITLE"},
                    .position = {960.0f, 540.0f, 0.0f},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = scale},
                    .layout = {.box = {1600.0f, 300.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{1.0f, 1.0f, 1.0f, alpha}},
                });
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/01/cinematic_title_reveal/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float scale = lerp_scale(frame_idx) * 0.6f;
                const float alpha = lerp_alpha(frame_idx);
                l.text("title", {
                    .content = {.value = "EPIC TITLE"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = scale},
                    .layout = {.box = {920.0f, 300.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{1.0f, 1.0f, 1.0f, alpha}},
                });
            });
            return s.build();
        });
}

} // namespace

// 16:9 snapshots
TEST_CASE("AE 01 cinematic_title_reveal 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    // doctest requires single binary comparisons per REQUIRE
    // (mirrors user_spec/11 canonical pattern).
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_01_cinematic_title_reveal_16x9_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
TEST_CASE("AE 01 cinematic_title_reveal 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_01_cinematic_title_reveal_16x9_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
TEST_CASE("AE 01 cinematic_title_reveal 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_01_cinematic_title_reveal_16x9_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

// 9:16 snapshots
TEST_CASE("AE 01 cinematic_title_reveal 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    // doctest requires single binary comparisons per REQUIRE
    // (mirrors user_spec/11 canonical pattern).
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_01_cinematic_title_reveal_9x16_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
TEST_CASE("AE 01 cinematic_title_reveal 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_01_cinematic_title_reveal_9x16_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
TEST_CASE("AE 01 cinematic_title_reveal 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_01_cinematic_title_reveal_9x16_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
