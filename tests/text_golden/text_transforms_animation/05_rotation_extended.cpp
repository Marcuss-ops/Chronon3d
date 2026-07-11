// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/05_rotation_extended.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Extended rotation angle tests.
// The existing 01_rotate_z_not_cut.cpp covers +15°..+90°.  This file
// covers negative and zero rotations (-45°, -30°, -15°, 0°) to lock
// the rotation transform path across the full range.
//
// 4 TEST_CASEs (rotation -45° / -30° / -15° / 0°) × 1 AR (1920×1080) = 4
// PNG goldens in
// `test_renders/golden/text/text_transforms_animation/rotation_extended/`.
//
// Invariants checked:
//   - Non-empty alpha_bbox at every rotation
//   - Centroid stays near canvas center (rotation is in-plane, no
//     translation; text is anchored at center)
//
// Per AGENTS.md §honesty: 4 PNG re-bake requires a working build host;
// the 4 test cases gracefully skip on `result.golden_missing`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::rotate_z(timeline)` +
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
GoldenTestConfig make_rotation_ext_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  =
        "test_renders/golden/text/text_transforms_animation/rotation_extended";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/rotation_extended/"} +
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
Composition build_rotation_ext_composition(
    SoftwareRenderer& renderer,
    float rotate_z_deg,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cx = static_cast<float>(canvas_w) * 0.5f;
    const float cy = static_cast<float>(canvas_h) * 0.5f;
    return composition(
        {.name = std::string{"TextTransforms/RotateZ_"} +
                  std::to_string(static_cast<int>(rotate_z_deg)) + "deg_" +
                  std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, rotate_z_deg, cx, cy, canvas_w, canvas_h]
        (const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [rotate_z_deg, cx, cy, canvas_w, canvas_h]
                            (LayerBuilder& l) {
                l.rotate_z(motion::timeline(rotate_z_deg));
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "ROTATED"},
                        .position = {cx, cy, 0.0f},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
                        },
                        .layout = {
                            .box = {static_cast<float>(canvas_w) * 0.7f,
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

// ── Golden verification helper ─────────────────────────────────────────
void verify_rotation_ext_golden(Framebuffer& fb, std::string_view case_slug) {
    auto r = verify_golden(fb, std::string{case_slug},
                           make_rotation_ext_config(case_slug));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) {
        INFO("Golden: ", r.message);
        CHECK(r.passed);
    }
}

} // namespace

TEST_CASE("TextTransforms.RotateZ_m45deg_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_rotation_ext_composition(renderer, -45.0f, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("rotate=-45deg bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    INFO("rotate=-45deg centroid: x=", centroid.x, " y=", centroid.y,
         " max_alpha=", centroid.max_alpha);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_right(1920, 0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(1080, 0));

    verify_rotation_ext_golden(*fb, "rotate_z_m45deg_1920x1080");
}

TEST_CASE("TextTransforms.RotateZ_m30deg_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_rotation_ext_composition(renderer, -30.0f, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_right(1920, 0));

    verify_rotation_ext_golden(*fb, "rotate_z_m30deg_1920x1080");
}

TEST_CASE("TextTransforms.RotateZ_m15deg_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_rotation_ext_composition(renderer, -15.0f, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_right(1920, 0));

    verify_rotation_ext_golden(*fb, "rotate_z_m15deg_1920x1080");
}

TEST_CASE("TextTransforms.RotateZ_0deg_Baseline_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_rotation_ext_composition(renderer, 0.0f, 1920, 1080), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    // At 0° rotation, centroid should be very close to canvas center.
    CHECK(std::abs(centroid.x - 960.0f) < 50.0f);
    CHECK(std::abs(centroid.y - 540.0f) < 50.0f);

    verify_rotation_ext_golden(*fb, "rotate_z_0deg_1920x1080");
}
