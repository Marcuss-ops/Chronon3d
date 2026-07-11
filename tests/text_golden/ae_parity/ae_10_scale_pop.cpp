// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_10_scale_pop.cpp
//
// TICKET-AE-PARITY-CINEMATIC-10 — Scene 10: scale_pop (spring overshoot
// entrance — text starts small, overshoots past 1.0, settles to 1.0).
// The two-channel co-modulation (scale + opacity fade-in) evokes a
// classic AE pop entrance without requiring a real spring solver
// (which is forward-only per Blocco 7 of the kinetic-typography roadmap):
//
//   Channel    │ f00                │ f15                │ f30                │
//   ────────────┼────────────────────┼────────────────────┼────────────────────│
//   scale      │ 0.50 (entry)       │ 1.30 (overshoot)   │ 1.00 (settle)      │
//   opacity    │ 0.00 (invisible)   │ 0.80 (mid-fade)    │ 1.00 (full)        │
//
// Reads as "the text springs into existence" — overshoots its target
// scale by 30% at f15, then settles to natural size at f30 with full
// opacity. Pure layer-level scale (whole text as a single rigid body);
// TRUE per-character pop is forward-only Phase 2 Killer scope.
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots f00/f15/f30.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `LayerBuilder::scale(...)` + `LayerBuilder::opacity(...)` +
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
    cfg.artifact_directory = "test_renders/artifacts/text/ae_10_scale_pop";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 9.0f / 255.0f;
    cfg.threshold.max_abs_error            = 125.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.92f;
    cfg.threshold.max_rmse                 = 11.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.65f;
    return cfg;
}

static Vec3 scale_for(std::size_t f) {
    if (f == 0)  return Vec3{0.50f, 0.50f, 1.0f};
    if (f <= 15) return Vec3{1.30f, 1.30f, 1.0f};
    return         Vec3{1.00f, 1.00f, 1.0f};
}

static float opacity_for(std::size_t f) {
    if (f == 0)  return 0.00f;
    if (f <= 15) return 0.80f;
    return 1.00f;
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/10/scale_pop/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                l.text("scale_pop", {
                    .content = {.value = "POP IN"},
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
                l.scale(scale_for(frame_idx));
                l.opacity(opacity_for(frame_idx));
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/10/scale_pop/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                l.text("scale_pop", {
                    .content = {.value = "POP IN"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 170.0f},
                    .layout = {.box = {1000.0f, 280.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
                l.scale(scale_for(frame_idx));
                l.opacity(opacity_for(frame_idx));
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 10 scale_pop 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_10_scale_pop_16x9_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 10 scale_pop 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_10_scale_pop_16x9_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 10 scale_pop 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_10_scale_pop_16x9_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 10 scale_pop 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_10_scale_pop_9x16_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 10 scale_pop 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_10_scale_pop_9x16_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 10 scale_pop 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_10_scale_pop_9x16_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
