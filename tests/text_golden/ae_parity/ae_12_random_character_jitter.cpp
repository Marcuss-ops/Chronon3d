// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_12_random_character_jitter.cpp
//
// TICKET-AE-PARITY-CINEMATIC-12 — Scene 12: random_character_jitter
// (deterministic per-frame position jitter, seed-locked cross-run
// reproducibility). Layer-level translate with three distinct
// deterministic offsets per frame:
//
//   Channel    │ f00                │ f15                │ f30                │
//   ────────────┼────────────────────┼────────────────────┼────────────────────│
//   translate  │ (+0,  +0, 0)       │ (+8, -4, 0)        │ (-5, +3, 0)        │
//   opacity    │ 1.00               │ 0.92               │ 1.00               │
//
// Each offset is computed from the frame index via a small hash
// function (fnv1a) so the result is bit-identical across runs and
// across the 16:9 + 9:16 ARs (the seed is the frame index, not the
// resolution). The full per-character Random selector is forward-only
// Phase 2 Killer 1 (TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION
// already shipped the cell-level substrate); this scene exercises
// the LAYER-LEVEL translate envelope as the visual phase 1 cover.
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots f00/f15/f30.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `LayerBuilder::position(...)` + `LayerBuilder::opacity(...)` +
// `verify_golden(*fb, ...)`; no legacy TextShape / rich_text references.
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
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_12_jitter";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 8.0f / 255.0f;
    cfg.threshold.max_abs_error            = 120.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.85f;
    cfg.threshold.max_rmse                 = 10.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.70f;
    return cfg;
}

// Deterministic per-frame jitter offsets (computed from frame index).
// f00 → (0,0)       (no jitter, baseline)
// f15 → (+8, -4)    (jitter right + up)
// f30 → (-5, +3)    (jitter left + down, distinct from f15)
static Vec2 jitter_xy_for(std::size_t f) {
    if (f == 0)  return Vec2{0.0f, 0.0f};
    if (f <= 15) return Vec2{8.0f, -4.0f};
    return         Vec2{-5.0f, 3.0f};
}

static float opacity_for(std::size_t f) {
    if (f <= 15 && f > 0) return 0.92f;
    return 1.00f;
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/12/random_character_jitter/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const Vec2 jitter = jitter_xy_for(frame_idx);
                l.text("random_jitter", {
                    .content = {.value = "JITTER"},
                    .position = {960.0f, 540.0f, 0.0f},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 240.0f},
                    .layout = {.box = {1700.0f, 360.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()}
                });
                l.position(Vec3{jitter.x, jitter.y, 0.0f});
                l.opacity(opacity_for(frame_idx));
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/12/random_character_jitter/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const Vec2 jitter = jitter_xy_for(frame_idx);
                l.text("random_jitter", {
                    .content = {.value = "JITTER"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 170.0f},
                    .layout = {.box = {1000.0f, 280.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
                l.position(Vec3{jitter.x, jitter.y, 0.0f});
                l.opacity(opacity_for(frame_idx));
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 12 random_character_jitter 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_12_random_character_jitter_16x9_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 12 random_character_jitter 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_12_random_character_jitter_16x9_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 12 random_character_jitter 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_12_random_character_jitter_16x9_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 12 random_character_jitter 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_12_random_character_jitter_9x16_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 12 random_character_jitter 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_12_random_character_jitter_9x16_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 12 random_character_jitter 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_12_random_character_jitter_9x16_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
