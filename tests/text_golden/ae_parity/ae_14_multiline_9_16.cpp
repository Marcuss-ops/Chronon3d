// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_14_multiline_9_16.cpp
//
// TICKET-AE-PARITY-CINEMATIC-14 — Scene 14: multiline_9_16_safezone
// (Titolo TikTok/Reels — multiline text in 9:16 portrait with explicit
// safe-zone constraint). The 9:16 AR has a typical "safe area" inset
// of ~10% on each side (108px on a 1080-wide canvas) to avoid the
// platform UI overlays. The scene exercises multiline text wrap inside
// a tight box that respects the safe-zone:
//
//   Channel    │ f00                │ f15                │ f30                │
//   ────────────┼────────────────────┼────────────────────┼────────────────────│
//   opacity    │ 0.00 (fade-in)     │ 0.70 (mid-fade)    │ 1.00 (full)        │
//   translate  │ (0, +20, 0)        │ (0,  +8, 0)        │ (0,  0, 0)         │
//
// Multiline text (3 lines) + 9:16 + safe-zone-aware box. Reads as
// a TikTok-style 3-line title with a small upward settle during the
// fade-in. The 16:9 variant exercises the same multiline text in
// a wider box (no safe-zone constraint) for cross-AR comparison.
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
    cfg.artifact_directory = "test_renders/artifacts/text/ae_14_multiline";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 9.0f / 255.0f;
    cfg.threshold.max_abs_error            = 125.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.92f;
    cfg.threshold.max_rmse                 = 11.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.65f;
    return cfg;
}

static float opacity_for(std::size_t f) {
    if (f == 0)  return 0.00f;
    if (f <= 15) return 0.70f;
    return 1.00f;
}

// Upward settle during fade-in (small translate to make the fade feel
// like a slow camera pull rather than a static fade).
static float translate_y_for(std::size_t f) {
    if (f == 0)  return 20.0f;
    if (f <= 15) return 8.0f;
    return 0.0f;
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/14/multiline_9_16/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float dy = translate_y_for(frame_idx);
                l.text("multiline", {
                    .content = {.value = "LINE ONE\nLINE TWO\nLINE THREE"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 120.0f},
                    .layout = {.box = {1600.0f, 700.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle,
                               .max_lines = 3},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, 540.0f + dy, 0.0f}
                });
                l.opacity(opacity_for(frame_idx));
            });
            return s.build();
        });
}

// 9:16 with 108px safe-zone inset (10% on each side of 1080).
// Box = (1080 - 2*108, ~700) = (864, 700). Vertical center stays at 960.
Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/14/multiline_9_16/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float dy = translate_y_for(frame_idx);
                l.text("multiline", {
                    .content = {.value = "LINE ONE\nLINE TWO\nLINE THREE"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 110.0f},
                    .layout = {.box = {864.0f, 700.0f},  // 10% safe-zone inset
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle,
                               .max_lines = 3},
                    .appearance = {.color = Color::white()},
                    .position = {540.0f, 960.0f + dy, 0.0f}
                });
                l.opacity(opacity_for(frame_idx));
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 14 multiline_9_16 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_14_multiline_9_16_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 14 multiline_9_16 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_14_multiline_9_16_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 14 multiline_9_16 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_14_multiline_9_16_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 14 multiline_9_16 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_14_multiline_9_16_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 14 multiline_9_16 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_14_multiline_9_16_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 14 multiline_9_16 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_14_multiline_9_16_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}
