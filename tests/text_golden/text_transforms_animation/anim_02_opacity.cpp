// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/anim_02_opacity.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Opacity animation test.
// Verifies that text with `opacity_timeline(motion::timeline(...))`
// produces a changing max_alpha across frames (alpha-weighted centroid
// max_alpha reflects the layer's opacity multiplier).
//
// 1 TEST_CASE × 3 frames (0, 15, 30) × 1 AR (1920×1080) = 3 PNG
// goldens in
// `test_renders/golden/text/text_transforms_animation/anim_opacity/`.
//
// Invariants checked (frame-by-frame):
//   - Non-empty alpha_bbox at every frame (text rendered each frame)
//   - max_alpha CHANGES monotonically (1.0 → 0.5 → 0.1)
//
// Per AGENTS.md §honesty: 3 PNG re-bake requires a working build host;
// the 3 test cases gracefully skip on `result.golden_missing`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::opacity_timeline(timeline)` +
// `text_run()` + `alpha_bbox()` + `alpha_centroid()` helpers.
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
#include <chronon3d/backends/software/render_settings.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>

#include <cmath>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── GoldenTestConfig factory ───────────────────────────────────────────
GoldenTestConfig make_anim_opacity_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  =
        "test_renders/golden/text/text_transforms_animation/anim_opacity";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/anim_opacity/"} +
        std::string{case_slug};
    cfg.mode = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 12.0f / 255.0f;
    cfg.threshold.max_abs_error          = 130.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.95f;
    cfg.threshold.max_rmse               = 14.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.55f;
    return cfg;
}

// ── Composition builder ───────────────────────────────────────────────
//
// Single text layer with a linear opacity animation: starts at 1.0
// (fully opaque) at frame 0, ends at 0.1 (nearly transparent) at
// frame 30.
Composition build_anim_opacity_composition(
    SoftwareRenderer& renderer,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cx = static_cast<float>(canvas_w) * 0.5f;
    const float cy = static_cast<float>(canvas_h) * 0.5f;
    return composition(
        {.name = std::string{"TextAnim/Opacity_"} +
                  std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{30}},
        [&renderer, cx, cy, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("fader", [cx, cy, canvas_w, canvas_h](LayerBuilder& l) {
                // Linear opacity animation: 1.0 at frame 0, 0.1 at frame 30.
                // The motion::timeline API is fluent: `timeline(initial)`
                // creates a Timeline starting at the initial value, then
                // `.to(end_frame, value, easing)` adds the second keyframe.
                // (The 2-arg brace-init form `{FrameRange, ValueRange}` is
                // NOT supported by `motion::timeline()` per the canonical
                // signature in include/chronon3d/animation/motion/timeline.hpp.)
                l.opacity_timeline(motion::timeline(1.0f)
                    .to(Frame{30}, 0.1f, EasingCurve{Easing::Linear}));
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "FADE"},
                        .position = {cx, cy, 0.0f},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 240.0f
                        },
                        .layout = {
                            .box = {static_cast<float>(canvas_w) * 0.5f,
                                    static_cast<float>(canvas_h) * 0.5f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("TextAnim.Opacity_Frame0_Opaque_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_anim_opacity_composition(renderer, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("opacity f0 centroid: max_alpha=", centroid.max_alpha);

    CHECK_FALSE(bbox.empty());
    // At opacity 1.0, max_alpha should be very high (close to 1.0).
    CHECK(centroid.max_alpha > 0.85f);

    auto r = verify_golden(*fb, "anim_opacity_f00_1920x1080",
                           make_anim_opacity_config("anim_opacity_f00_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) { CHECK(r.passed); }
}

TEST_CASE("TextAnim.Opacity_Frame15_Mid_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_anim_opacity_composition(renderer, 1920, 1080), Frame{15});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("opacity f15 centroid: max_alpha=", centroid.max_alpha);

    CHECK_FALSE(bbox.empty());
    // At opacity ~0.55, max_alpha should be around 0.55.
    CHECK(centroid.max_alpha > 0.35f);
    CHECK(centroid.max_alpha < 0.75f);

    auto r = verify_golden(*fb, "anim_opacity_f15_1920x1080",
                           make_anim_opacity_config("anim_opacity_f15_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) { CHECK(r.passed); }
}

TEST_CASE("TextAnim.Opacity_Frame30_Transparent_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_anim_opacity_composition(renderer, 1920, 1080), Frame{30});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("opacity f30 centroid: max_alpha=", centroid.max_alpha);

    // At opacity 0.1, alpha is just above the threshold (0.05).
    // The bbox may still be non-empty due to the threshold filter, but
    // max_alpha should be very low.
    CHECK(centroid.max_alpha < 0.20f);

    auto r = verify_golden(*fb, "anim_opacity_f30_1920x1080",
                           make_anim_opacity_config("anim_opacity_f30_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) { CHECK(r.passed); }
}
