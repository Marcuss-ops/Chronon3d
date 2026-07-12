// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/04_parent_transform.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Parent transform test.
// Verifies that a child layer with `parent("layer_name")` is correctly
// offset by the parent's transform chain.  Two cases: (a) parent at
// +500 X → effective centroid at +500 X; (b) parent at -300 X → effective
// centroid at -300 X.  The negative-offset case exercises a different
// branch of the world-matrix composition path.
//
// 2 TEST_CASEs (Parent +500 X / Parent -300 X) × 1 AR (1920×1080) = 2
// PNG goldens in
// `test_renders/golden/text/text_transforms_animation/parent_transform/`.
//
// Invariants checked:
//   - Non-empty alpha_bbox (text actually rendered at child layer)
//   - Centroid X is offset by parent's position (both + and - offsets)
//   - Centroid Y is near child position (no Y offset from parent)
//   - INFO() diagnostic surfaces effective position so a regression
//     does NOT silently pass (e.g., if the render graph treats
//     position-only parent layers as a no-op, the diagnostic shows
//     the actual centroid vs the expected one).
//
// Per AGENTS.md §honesty: 2 PNG re-bake requires a working build host;
// the 2 test cases gracefully skip on `result.golden_missing`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::parent()` + `text_run()` +
// `alpha_centroid()` helpers.
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
GoldenTestConfig make_parent_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  =
        "test_renders/golden/text/text_transforms_animation/parent_transform";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/parent_transform/"} +
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
// 2-layer composition:
//   1. "parent_layer"  — invisible placeholder at `parent_offset_x`
//   2. "child_layer"   — text "CHILD" at canvas center with
//                        `parent("parent_layer")`
//
// The parent is position-only (no visible node) — if the render graph
// treats a position-only parent as a no-op, the diagnostic will surface
// `centroid.x ≈ 960` (child center) instead of `960 + parent_offset_x`.
// See test_helpers.hpp:alpha_centroid() for the centroid extraction.
Composition build_parent_transform_composition(
    SoftwareRenderer& renderer,
    f32 parent_offset_x,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    return composition(
        {.name = std::string{"TextTransforms/ParentTransform_"} +
                  (parent_offset_x >= 0 ? std::string{"p"} : std::string{"m"}) +
                  std::to_string(static_cast<int>(std::abs(parent_offset_x))) +
                  "_" + std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, parent_offset_x, canvas_w, canvas_h]
        (const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            // Parent layer (invisible, no text — just provides transform).
            s.layer("parent_layer",
                    [parent_offset_x, canvas_h](LayerBuilder& l) {
                l.position({parent_offset_x, static_cast<float>(canvas_h) * 0.5f, 0.0f});
            });
            // Child layer with parent pointer.
            s.layer("child_layer", [canvas_w, canvas_h](LayerBuilder& l) {
                l.parent("parent_layer");
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "CHILD"},
                        .position = {static_cast<float>(canvas_w) * 0.5f,
                                     static_cast<float>(canvas_h) * 0.5f,
                                     0.0f},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
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

// ── Case (a): parent at +500 X → centroid at 960 + 500 = 1460 X ────────
TEST_CASE("TextTransforms.ParentTransform_Plus500X_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_parent_transform_composition(renderer, +500.0f, 1920, 1080),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("parent=+500X bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    INFO("parent=+500X centroid: x=", centroid.x, " y=", centroid.y,
         " max_alpha=", centroid.max_alpha);

    // Non-empty invariant.
    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: parent's +500 X offset shifts the centroid
    // right.  Child is at canvas center (960), so effective centroid
    // should be near 1460 (within tolerance).
    const float expected_cx = 960.0f + 500.0f;  // = 1460
    const float cx_tolerance = 200.0f;
    CHECK(std::abs(centroid.x - expected_cx) < cx_tolerance);
    CHECK(std::abs(centroid.y - 540.0f) < 200.0f);

    auto r = verify_golden(*fb, "parent_transform_p500_1920x1080",
                           make_parent_config("parent_transform_p500_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) {
        INFO("Golden: ", r.message);
        CHECK(r.passed);
    }
}

// ── Case (b): parent at -300 X → centroid at 960 - 300 = 660 X ──────────
TEST_CASE("TextTransforms.ParentTransform_Minus300X_1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_parent_transform_composition(renderer, -300.0f, 1920, 1080),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const AlphaCentroid centroid = alpha_centroid(*fb);
    INFO("parent=-300X bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    INFO("parent=-300X centroid: x=", centroid.x, " y=", centroid.y,
         " max_alpha=", centroid.max_alpha);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: parent's -300 X offset shifts the centroid left.
    // Child is at canvas center (960), so effective centroid should be
    // near 660 (within tolerance).
    const float expected_cx = 960.0f - 300.0f;  // = 660
    const float cx_tolerance = 200.0f;
    CHECK(std::abs(centroid.x - expected_cx) < cx_tolerance);
    CHECK(std::abs(centroid.y - 540.0f) < 200.0f);

    auto r = verify_golden(*fb, "parent_transform_m300_1920x1080",
                           make_parent_config("parent_transform_m300_1920x1080"));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) {
        INFO("Golden: ", r.message);
        CHECK(r.passed);
    }
}
