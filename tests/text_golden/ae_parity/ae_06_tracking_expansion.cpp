// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_06_tracking_expansion.cpp
//
// TICKET-AE-PARITY-CINEMATIC-06 — Scene 06: tracking_expansion (title trailer
// reveal). Three simultaneous progressive animation channels per frame
// snapshot — the AE-equivalent of a cinematic title-card opening sequence:
//
//   Channel │ f00                │ f15                │ f30                │
//   ─────────┼────────────────────┼────────────────────┼────────────────────│
//   tracking │ 4.0 f (tight)      │ 12.0 f (mid)       │ 22.0 f (climax)    │
//   opacity  │ 0.30 (faded start) │ 0.70 (mid-reveal)  │ 1.00 (full)        │
//   blur     │ 20.0 (tier 4)      │ 7.0 (tier 2)       │ 0.0 (tier 0)       │
//
// tracking  → .layout.tracking text-level field (LayoutSpec)
// opacity   → LayerBuilder::opacity(f32)            (layer_builder.cpp:138)
// blur      → LayerBuilder::blur(f32)               (layer_builder.cpp:186)
//             ↳ bucket_radius_for_tier → kBlurTierRadii = {{0,2,7,13,20}}
//               → apply_separable_box_blur (text_run_processor/scratch.cpp:70)
//
// Cross-link: ae_09_blur_in scene locks the blur ladder (already shipped);
// ae_06_tracking_expansion locks the title-trailer reveal triple animation
// (tracking + fade-in + blur resolve). The geometric mean_abs_diff delta
// across f00→f15→f30 captures the multi-channel reveal motion into reproducible
// golden PNGs.
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `LayerBuilder::opacity(...)` + `LayerBuilder::blur(...)` +
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
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_06_tracking";
    cfg.mode               = golden_mode_from_environment();
    // Acceptance contract: each frame MUST differ materially from f00
    // baseline (multi-channel motion: tracking + opacity + blur → spread).
    // Looser thresholds because the rendered frame's text geometry changes
    // between f00/f15/f30 (letter spacing grows + opacity lifts + blur
    // resolves); threshold is regression-locked per-frame, not a cross-frame
    // diff (each frame's saved golden is its own reference).
    cfg.threshold.max_mean_abs_error       = 8.0f / 255.0f;
    cfg.threshold.max_abs_error            = 120.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.95f;
    cfg.threshold.max_rmse                 = 10.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.70f;
    return cfg;
}

// Per-frame tracking amount (extra letter spacing in normalized font units).
// f=0  → 4.0  (slightly tight, faded start)
// f<=15→ 12.0 (mid tracking, mid-reveal)
// f>15 → 22.0 (high tracking, trailer climax)
static float tracking_for(std::size_t f) {
    if (f == 0)  return 4.0f;
    if (f <= 15) return 12.0f;
    return 22.0f;
}

// Per-frame opacity (cross-link to LayerBuilder::opacity).
static float opacity_for(std::size_t f) {
    if (f == 0)  return 0.30f;
    if (f <= 15) return 0.70f;
    return 1.00f;
}

// Per-frame blur reveal (cross-link to kBlurTierRadii = {{0, 2, 7, 13, 20}}).
// f=0  → 20.0f (tier 4, max blur, ghosted reveal-start)
// f<=15→ 7.0f  (tier 2, mid blur)
// f>15 → 0.0f  (tier 0, fully resolved)
static float blur_for(std::size_t f) {
    if (f == 0)  return 20.0f;
    if (f <= 15) return 7.0f;
    return 0.0f;
}

// 16:9 (1920x1080) cinematic title-card trailer reveal.
Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/06/tracking_expansion/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "TITLE TRAILER"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 220.0f},
                    .layout = {.box = {1700.0f, 360.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle,
                               .tracking = tracking_for(frame_idx)},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, 540.0f, 0.0f}
                });
                l.opacity(opacity_for(frame_idx));
                l.blur(blur_for(frame_idx));
            });
            return s.build();
        });
}

// 9:16 (1080x1920) portrait variant — same motion envelope, tighter box.
Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/06/tracking_expansion/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "TITLE TRAILER"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 156.0f},
                    .layout = {.box = {1000.0f, 280.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle,
                               .tracking = tracking_for(frame_idx)},
                    .appearance = {.color = Color::white()},
                    .position = {540.0f, 960.0f, 0.0f}
                });
                l.opacity(opacity_for(frame_idx));
                l.blur(blur_for(frame_idx));
            });
            return s.build();
        });
}

} // namespace

// ─── 16:9 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 06 tracking_expansion 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_06_tracking_expansion_16x9_f00", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 06 tracking_expansion 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_06_tracking_expansion_16x9_f15", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 06 tracking_expansion 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_06_tracking_expansion_16x9_f30", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

// ─── 9:16 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 06 tracking_expansion 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_06_tracking_expansion_9x16_f00", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 06 tracking_expansion 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_06_tracking_expansion_9x16_f15", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("AE 06 tracking_expansion 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_06_tracking_expansion_9x16_f30", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
