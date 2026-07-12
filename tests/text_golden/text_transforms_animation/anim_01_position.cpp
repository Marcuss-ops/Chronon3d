// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/anim_01_position.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Position animation test.
// Verifies that text with `position_x(motion::timeline(...))` (a
// position animation in X) produces a monotonically shifting centroid
// across the animation frames.
//
// 1 TEST_CASE × 3 frames (0, 15, 30) × 1 AR (1920×1080) = 3 PNG
// goldens in
// `test_renders/golden/text/text_transforms_animation/anim_position/`.
//
// Invariants checked (frame-by-frame):
//   - Non-empty alpha_bbox at every frame (text rendered each frame)
//   - Centroid X position INCREASES across frames (linear X translation)
//   - Centroid Y stays roughly constant (X-only animation)
//
// Per AGENTS.md §honesty: 3 PNG re-bake requires a working build host;
// the 3 test cases gracefully skip on `result.golden_missing`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::position_x(timeline)` +
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
GoldenTestConfig make_anim_pos_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  =
        "test_renders/golden/text/text_transforms_animation/anim_position";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/anim_position/"} +
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
// Single text layer with a linear X-translation animation: starts at
// x=400 at frame 0, ends at x=1520 at frame 30 (linear).
Composition build_anim_position_composition(
    SoftwareRenderer& renderer,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cy = static_cast<float>(canvas_h) * 0.5f;
    return composition(
        {.name = std::string{"TextAnim/Position_"} +
                  std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{30}},
        [&renderer, cy, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("mover", [cy, canvas_w, canvas_h](LayerBuilder& l) {
                // Linear X translation: 400 at frame 0, 1520 at frame 30.
                // The motion::timeline API is fluent: `timeline(initial)`
                // creates a Timeline starting at the initial value, then
                // `.to(end_frame, value, easing)` adds the second keyframe.
                // (The 2-arg brace-init form `{FrameRange, ValueRange}` is
                // NOT supported by `motion::timeline()` per the canonical
                // signature in include/chronon3d/animation/motion/timeline.hpp.)
                l.position_x(motion::timeline(400.0f)
                    .to(Frame{30}, 1520.0f, EasingCurve{Easing::Linear}));
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "MOVE"},
                        .position = {static_cast<float>(canvas_w) * 0.5f, cy, 0.0f},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
                        },
                        .layout = {
                            .box = {static_cast<float>(canvas_w) * 0.3f,
                                    static_cast<float>(canvas_h) * 0.4f},
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

TEST_CASE("TextAnim.Position_Frame0_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_anim_position_composition(renderer, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("position f0 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    INFO("position f0 centroid: x=", centroid.x, " y=", centroid.y);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    // At frame 0, the text is at x=400 (left of canvas).
    CHECK(centroid.x < 600.0f);
    CHECK(std::abs(centroid.y - 540.0f) < 200.0f);

    auto r = verify_golden(*fb, "anim_position_f00_1920x1080",
                           make_anim_pos_config("anim_position_f00_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) { CHECK(r.passed); }
}

TEST_CASE("TextAnim.Position_Frame15_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_anim_position_composition(renderer, 1920, 1080), Frame{15});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("position f15 centroid: x=", centroid.x, " y=", centroid.y);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    // At frame 15, midpoint of 400..1520 → ~960 (canvas center).
    CHECK(centroid.x > 800.0f);
    CHECK(centroid.x < 1100.0f);

    auto r = verify_golden(*fb, "anim_position_f15_1920x1080",
                           make_anim_pos_config("anim_position_f15_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) { CHECK(r.passed); }
}

TEST_CASE("TextAnim.Position_Frame30_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_anim_position_composition(renderer, 1920, 1080), Frame{30});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("position f30 centroid: x=", centroid.x, " y=", centroid.y);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    // At frame 30, the text is at x=1520 (right of canvas).
    CHECK(centroid.x > 1400.0f);
    CHECK(std::abs(centroid.y - 540.0f) < 200.0f);

    auto r = verify_golden(*fb, "anim_position_f30_1920x1080",
                           make_anim_pos_config("anim_position_f30_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) { CHECK(r.passed); }
}
