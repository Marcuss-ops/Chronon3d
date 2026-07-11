// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_09_blur_in.cpp
//
// TICKET-AE-PARITY-CINEMATIC-09 — Scene 09: blur_in (kinetic blur ladder lock).
// "CHRONON3D" hero text, static across timeline, with progressive Gaussian
// blur tier applied at each frame snapshot. The blur is layered onto the text
// layer via LayerBuilder::blur(float r), which the renderer internally
// classifies via `detail::bucket_radius_for_tier` (text_run_processor.hpp:70)
// into one of the 5 canonical tiers `kBlurTierRadii = {{0, 2, 7, 13, 20}}`
// (text_run_processor/text_run_stages.hpp:51). Each tier then dispatches
// `apply_separable_box_blur` (scratch.cpp:70) at the resolved radius.
//
// Frame snapshot mapping (locks the 5/255 mean_abs_diff + 0.10 pixel_changed_ratio
// acceptance contract vs the f00 baseline):
//   f00  → blur=0.0f  (tier 0, no blur applied)            → BASELINE
//   f15  → blur=7.0f  (tier 2, exact match in ladder)     → progressive mid
//   f30  → blur=20.0f (tier 4, max-radius tier)            → progressive max
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API; verify_golden
// helper reuse from tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `LayerBuilder::blur(...)` + `verify_golden(*fb, ...)`; no legacy TextShape
// / rich_text references.
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
    cfg.artifact_directory = "test_renders/artifacts/text/ae_09_blur";
    cfg.mode               = golden_mode_from_environment();
    // thresholds: tightened vs ae_01 because blur progression makes
    // successive frames materially different; higher max_abs_error +
    // higher max_changed_pixel_ratio to accommodate 0→7→20 blur ladder.
    cfg.threshold.max_mean_abs_error       = 8.0f / 255.0f;
    cfg.threshold.max_abs_error            = 120.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.95f;
    cfg.threshold.max_rmse                 = 10.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.70f;
    return cfg;
}

// Per-frame blur tier (locked against kBlurTierRadii = {{0, 2, 7, 13, 20}}):
//   f=0  → 0.0f → bucket tier 0 (no blur)         [baseline]
//   f<=15 → 7.0f → bucket tier 2 (radius 7 exact) [mid blur]
//   f>15 → 20.0f → bucket tier 4 (radius 20)      [max blur]
static float blur_for(std::size_t f) {
    if (f == 0)  return 0.0f;
    if (f <= 15) return 7.0f;
    return 20.0f;
}

// 16:9 (1920x1080) static text + per-frame Gaussian blur tier.
Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/09/blur_in/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "CHRONON3D"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 240.0f},
                    .layout = {.box = {1600.0f, 480.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, 540.0f, 0.0f}
                });
                // Per-frame blur tier; bucket_radius_for_tier in
                // text_run_processor.hpp:70 maps the value to the nearest
                // kBlurTierRadii entry before apply_separable_box_blur.
                l.blur(blur_for(frame_idx));
            });
            return s.build();
        });
}

// 9:16 (1080x1920) portrait variant — same blur ladder, smaller text scale.
Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/09/blur_in/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "CHRONON3D"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 160.0f},
                    .layout = {.box = {920.0f, 360.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {540.0f, 960.0f, 0.0f}
                });
                l.blur(blur_for(frame_idx));
            });
            return s.build();
        });
}

} // namespace

// ─── 16:9 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 09 blur_in 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_09_blur_in_16x9_f00", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 09 blur_in 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_09_blur_in_16x9_f15", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 09 blur_in 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_09_blur_in_16x9_f30", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

// ─── 9:16 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 09 blur_in 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_09_blur_in_9x16_f00", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 09 blur_in 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_09_blur_in_9x16_f15", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 09 blur_in 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_09_blur_in_9x16_f30", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
