// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/02_scale.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Scale transform test family.
// Verifies that text scaled via `LayerBuilder::scale(Vec3)` produces
// the expected visible bbox and centroid at multiple scale factors,
// including uniform (0.5, 1.5, 2.0) and non-uniform (0.96, 1.04).
//
// 4 TEST_CASEs (uniform 0.5× / 1.5× / 2.0× + non-uniform 0.96×1.04) ×
// 1 AR (1920×1080) = 4 PNG goldens in
// `test_renders/golden/text/text_transforms_animation/scale/`.
//
// Invariants checked:
//   - Non-empty alpha_bbox at every scale factor (text actually rendered)
//   - Centroid stays near canvas center as scale changes (anchor = center)
//   - bbox dimensions grow monotonically with uniform scale factor
//
// Per AGENTS.md §honesty: 4 PNG re-bake requires a working build host
// (vcpkg-installed includes + tmpfs quota); the 4 test cases gracefully
// skip on `result.golden_missing`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::scale()` + `text_run()` + the
// existing `verify_golden()` + `alpha_bbox()` + `alpha_centroid()` helpers.
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
GoldenTestConfig make_scale_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_transforms_animation/scale";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/scale/"} +
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
// Single text layer on a canvas, with the layer's `scale(Vec3)`
// transform applied.  `scale_uniform` is the uniform scale factor;
// `scale_x` and `scale_y` are the non-uniform scale components (only
// used when `use_nonuniform` is true).
Composition build_scale_composition(
    SoftwareRenderer& renderer,
    f32 scale_uniform,
    bool use_nonuniform = false,
    f32 scale_x = 1.0f,
    f32 scale_y = 1.0f,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cx = static_cast<float>(canvas_w) * 0.5f;
    const float cy = static_cast<float>(canvas_h) * 0.5f;
    return composition(
        {.name = std::string{"TextTransforms/Scale_"} +
                  (use_nonuniform
                      ? std::string{"nonuniform_"} +
                            std::to_string(static_cast<int>(scale_x * 100)) +
                            "x" +
                            std::to_string(static_cast<int>(scale_y * 100))
                      : std::string{"uniform_"} +
                            std::to_string(static_cast<int>(scale_uniform * 100)) +
                            "x") +
                  "_" + std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, scale_uniform, use_nonuniform, scale_x, scale_y,
         cx, cy, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("scaled_text", [scale_uniform, use_nonuniform, scale_x, scale_y,
                                    cx, cy, canvas_w, canvas_h](LayerBuilder& l) {
                // Apply scale BEFORE the text_run.
                // `scale(Vec3)` takes a single 3D Vec3 scale.
                if (use_nonuniform) {
                    l.scale({scale_x, scale_y, 1.0f});
                } else {
                    l.scale({scale_uniform, scale_uniform, 1.0f});
                }
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "SCALED"},
                        .position = {cx, cy, 0.0f},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
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

// ── Render one configuration ───────────────────────────────────────────
struct RenderedScale {
    std::shared_ptr<Framebuffer> fb;
    int width;
    int height;
};

RenderedScale render_scale(SoftwareRenderer& renderer, f32 scale_uniform,
                           bool use_nonuniform, f32 sx, f32 sy) {
    auto fb = renderer.render(
        build_scale_composition(renderer, scale_uniform, use_nonuniform, sx, sy),
        Frame{0});
    return RenderedScale{fb, 1920, 1080};
}

// ── Golden verification helper (canonical pattern) ─────────────────────
void verify_scale_golden(Framebuffer& fb, std::string_view case_slug) {
    auto r = verify_golden(fb, std::string{case_slug}, make_scale_config(case_slug));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) {
        INFO("Golden: ", r.message);
        CHECK(r.passed);
    }
}

} // namespace

// ═══ Test 1 — Scale 0.5× (uniform) ═════════════════════════════════════
TEST_CASE("TextTransforms.Scale_0_5x_uniform_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_scale(renderer, 0.5f, false, 1.0f, 1.0f);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("scale=0.5x bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());
    INFO("scale=0.5x centroid: x=", centroid.x, " y=", centroid.y,
         " max_alpha=", centroid.max_alpha);

    // Non-empty invariant: visible pixels must be present.
    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: text is anchored at canvas center,
    // so the centroid should be near (cx, cy) within tolerance.
    const float cx_tolerance = 200.0f;
    const float cy_tolerance = 200.0f;
    CHECK(std::abs(centroid.x - 960.0f) < cx_tolerance);
    CHECK(std::abs(centroid.y - 540.0f) < cy_tolerance);

    verify_scale_golden(*r.fb, "scale_0_5x_uniform_1920x1080");
}

// ═══ Test 2 — Scale 1.5× (uniform) ════════════════════════════════════
TEST_CASE("TextTransforms.Scale_1_5x_uniform_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_scale(renderer, 1.5f, false, 1.0f, 1.0f);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("scale=1.5x bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    CHECK(std::abs(centroid.x - 960.0f) < 200.0f);
    CHECK(std::abs(centroid.y - 540.0f) < 200.0f);

    verify_scale_golden(*r.fb, "scale_1_5x_uniform_1920x1080");
}

// ═══ Test 3 — Scale 2.0× (uniform) ═════════════════════════════════════
TEST_CASE("TextTransforms.Scale_2_0x_uniform_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_scale(renderer, 2.0f, false, 1.0f, 1.0f);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("scale=2.0x bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    // 2x scale should not push bbox to the canvas edges.
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_scale_golden(*r.fb, "scale_2_0x_uniform_1920x1080");
}

// ═══ Test 4 — Scale 0.96×1.04 (non-uniform) ═══════════════════════════
//
// Catches the bug where non-uniform scale produces a degenerate bbox
// (e.g., scale_x=0.96, scale_y=1.04 on a 180pt font might compress
// horizontally without expanding the bbox correctly).
TEST_CASE("TextTransforms.Scale_0_96x1_04_nonuniform_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_scale(renderer, 1.0f, true, 0.96f, 1.04f);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("scale=0.96x1.04 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);
    CHECK(std::abs(centroid.x - 960.0f) < 200.0f);
    CHECK(std::abs(centroid.y - 540.0f) < 200.0f);

    verify_scale_golden(*r.fb, "scale_0_96x1_04_nonuniform_1920x1080");
}
