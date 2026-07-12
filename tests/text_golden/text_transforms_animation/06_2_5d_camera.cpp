// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/06_2_5d_camera.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — 2.5D camera / 3D-enabled text
// rendering test.
//
// Verifies that text with `enable_3d(true)` + a small `depth_offset(50)`
// (pushing the layer 50 units forward in Z) renders correctly through
// the 2.5D camera pipeline (the perspective projection + the
// depth-aware compositor).
//
// 1 TEST_CASE × 1 AR (1920×1080) = 1 PNG golden in
// `test_renders/golden/text/text_transforms_animation/two_point_five_d/`.
//
// Invariants checked:
//   - Non-empty alpha_bbox (text rendered through 3D pipeline)
//   - Centroid near canvas center (depth_offset doesn't translate in X/Y)
//   - Bbox dimensions reasonable (text not scaled to zero by perspective)
//
// Per AGENTS.md §honesty: 1 PNG re-bake requires a working build host;
// missing goldens are now treated as HARD CI failures via
// `REQUIRE_FALSE(r.golden_missing)` (the canonical
// `text_completeness.cpp:151` pattern). A missing golden is an ERROR,
// not a skip — tests that ran with `result.golden_missing = true`
// previously silently passed (the §honesty rot).
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::enable_3d()` + `depth_offset()`
// + `text_run()` + `alpha_bbox()` + `alpha_centroid()` helpers.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

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
GoldenTestConfig make_2_5d_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  =
        "test_renders/golden/text/text_transforms_animation/two_point_five_d";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/two_point_five_d/"} +
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
Composition build_2_5d_composition(
    SoftwareRenderer& renderer,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cx = static_cast<float>(canvas_w) * 0.5f;
    const float cy = static_cast<float>(canvas_h) * 0.5f;
    return composition(
        {.name = std::string{"TextTransforms/TwoPointFiveD_"} +
                  std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, cx, cy, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("three_dee", [cx, cy, canvas_w, canvas_h](LayerBuilder& l) {
                // Enable 3D pipeline (perspective camera).
                l.enable_3d(true);
                // Push the layer 50 units forward in Z (subtle depth).
                l.depth_offset(50.0f);
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "2.5D"},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {cx, cy}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 240.0f
                        },
                        .layout = {
                            .box = {static_cast<float>(canvas_w) * 0.6f,
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

TEST_CASE("TextTransforms.TwoPointFiveD_Enabled_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_2_5d_composition(renderer, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("2.5d bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    INFO("2.5d centroid: x=", centroid.x, " y=", centroid.y,
         " max_alpha=", centroid.max_alpha);

    // Non-empty invariant.
    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: text anchored at center, depth doesn't
    // translate X/Y significantly.  Centroid should be near canvas
    // center within a generous tolerance.
    CHECK(std::abs(centroid.x - 960.0f) < 300.0f);
    CHECK(std::abs(centroid.y - 540.0f) < 300.0f);

    // Bbox invariant: 240pt text rendered through 3D pipeline should
    // still produce a reasonable bbox (not collapsed to 0 by perspective).
    CHECK(bbox.width()  > 100);
    CHECK(bbox.height() > 50);

    // TICKET-TEXT-GOLDEN-MISSING-FAIL-LOUD: a missing golden is a
    // REQUIRE failure (NOT a soft-skip), per the canonical
    // text_completeness.cpp:151 pattern + cert user spec.
    auto r = verify_golden(*fb, "two_point_five_d_enabled_1920x1080",
                           make_2_5d_config("two_point_five_d_enabled_1920x1080"));
    INFO("Golden: ", r.message);
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
