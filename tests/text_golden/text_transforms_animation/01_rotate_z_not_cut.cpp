// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — First test of the V0.2
// transforms/animation cluster.  Verifies that text rotated around the
// Z axis (in the canvas plane) does NOT clip at the canvas edges.
//
// 3 TEST_CASEs (RotateZ 15° / 45° / 90°) × 2 aspect ratios (1920×1080 +
// 1080×1920) = 6 PNG goldens total in
// `test_renders/golden/text/text_transforms_animation/rotate_z_not_cut/`.
//
// Per AGENTS.md §honesty: 6 PNG re-bake requires a working build host
// (vcpkg-installed includes + tmpfs quota for full cmake build on this
<<<<<<< HEAD
// VPS); missing goldens are now treated as hard CI failures via
// REQUIRE_FALSE(r.golden_missing).
=======
// VPS); the 6 test cases fail if the golden reference is missing (`REQUIRE_FALSE(result.golden_missing)`)
// (prints a developer-instructional MESSAGE).
>>>>>>> dbf39153 (fix(tests): make golden references mandatory in CI/certification mode)
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::rotate_z()` + `text_run()` + the
// existing `verify_golden()` helper.
//
// User spec verification:
//   - 3 rotations × 2 ARs = 6 PNG goldens
//   - All NOT cut: no edge touch + bbox height > 0 + width > 0
//   - Re-bake command: `CHRONON3D_UPDATE_GOLDENS=1 ctest -R
//     chronon3d_text_golden_tests --test-case="TextTransforms.RotateZ *"`
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
GoldenTestConfig make_rotate_z_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_transforms_animation/rotate_z_not_cut";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/rotate_z_not_cut/"} +
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
// Single text layer on a canvas.  Parameterized by `rotate_z_deg` (the
// rotation angle in degrees around the Z axis, in the canvas plane) and
// the canvas dimensions (for the 1920×1080 vs 1080×1920 AR matrix).
//
// The text "ROTATED TEXT" is rendered at 180pt, centered on the canvas,
// then the layer is rotated around its local Z axis (canvas Z) by
// `rotate_z_deg`.  This verifies that the rotation transform path
// (rotate_z → world_matrix → bbox) correctly expands the visible bbox
// WITHOUT clipping at the canvas edges.
Composition build_rotate_z_composition(
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
        [&renderer, rotate_z_deg, cx, cy, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Engine cascades automatically from SceneBuilder to LayerBuilder.
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [rotate_z_deg, cx, cy, canvas_w, canvas_h](LayerBuilder& l) {
                // Apply Z-axis rotation (in the canvas plane) BEFORE the
                // text_run.  `rotate(Vec3)` sets the layer's static
                // rotation.  This verifies that the rotation transform
                // path correctly produces a rotated text run without
                // clipping the visible ink at the canvas edges.
                l.rotate(Vec3{0.0f, 0.0f, rotate_z_deg});
                l.text_run("title", TextRunParams{
                    .text = {
                        // TextSpec field order: content, position, font,
                        // layout, appearance (C++20 designated-init order
                        // must match declaration order per spec).
                        .content = {.value = "ROTATED TEXT"},
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

// ── Golden verification helper ────────────────────────────────────────
void verify_rotate_z_golden(
    Framebuffer& fb,
    std::string_view case_slug
) {
    auto r = verify_golden(fb, std::string{case_slug},
                           make_rotate_z_config(case_slug));
    INFO("Golden: ", r.message);
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

// ── Render one AR for one rotation ─────────────────────────────────────
struct RenderedAR {
    std::shared_ptr<Framebuffer> fb;
    int width;
    int height;
};

RenderedAR render_rotate_z_ar(SoftwareRenderer& renderer, float rotate_z_deg,
                              int canvas_w, int canvas_h) {
    auto fb = renderer.render(
        build_rotate_z_composition(renderer, rotate_z_deg, canvas_w, canvas_h),
        Frame{0});
    return RenderedAR{fb, canvas_w, canvas_h};
}

} // namespace

// ═══ Test 1 — RotateZ 15° × 2 ARs ═══════════════════════════════════════
//
// Light rotation (15°).  The text is rotated slightly but should still
// fit within the canvas without clipping.  Catches the bug where the
// rotation transform produces a bbox that overflows the canvas
// (because the rotated text's AABB is larger than the original AABB).
//
// Verifies:
//   - visible_bbox is non-empty
//   - visible_bbox does not touch any edge
//   - visible_bbox height > 50  (text actually rendered)
//   - visible_bbox width  > 200
TEST_CASE("TextTransforms.RotateZ_15deg_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_rotate_z_ar(renderer, 15.0f, 1920, 1080);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    INFO("15deg-1920x1080 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 200);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(r.height, 0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_rotate_z_golden(*r.fb, "rotate_z_15deg_1920x1080");
}

TEST_CASE("TextTransforms.RotateZ_15deg_NotCut_1080x1920") {
    auto renderer = test::make_renderer();
    auto r = render_rotate_z_ar(renderer, 15.0f, 1080, 1920);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    INFO("15deg-1080x1920 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 200);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(r.height, 0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_rotate_z_golden(*r.fb, "rotate_z_15deg_1080x1920");
}

// ═══ Test 2 — RotateZ 45° × 2 ARs ═══════════════════════════════════════
//
// Medium rotation (45°).  At 45° the AABB of the rotated text is
// significantly larger than the original AABB (rotated 45° on a 1200×400
// box has a diagonal AABB of ~1131×1131).  This catches bugs where the
// rotation transform produces an AABB that overflows the canvas.
TEST_CASE("TextTransforms.RotateZ_45deg_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_rotate_z_ar(renderer, 45.0f, 1920, 1080);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    INFO("45deg-1920x1080 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 200);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(r.height, 0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_rotate_z_golden(*r.fb, "rotate_z_45deg_1920x1080");
}

TEST_CASE("TextTransforms.RotateZ_45deg_NotCut_1080x1920") {
    auto renderer = test::make_renderer();
    auto r = render_rotate_z_ar(renderer, 45.0f, 1080, 1920);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    INFO("45deg-1080x1920 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 200);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(r.height, 0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_rotate_z_golden(*r.fb, "rotate_z_45deg_1080x1920");
}

// ═══ Test 3 — RotateZ 90° × 2 ARs ═══════════════════════════════════════
//
// Full 90° rotation.  At 90° the AABB of a 1200×400 box becomes
// 400×1200 (the box dimensions are swapped).  Catches bugs where the
// rotation transform produces a bbox that overflows the canvas in the
// narrow dimension (1920×1080 = 1080 vertical, 1080×1920 = 1080 horizontal).
TEST_CASE("TextTransforms.RotateZ_90deg_NotCut_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_rotate_z_ar(renderer, 90.0f, 1920, 1080);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    INFO("90deg-1920x1080 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 200);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(r.height, 0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_rotate_z_golden(*r.fb, "rotate_z_90deg_1920x1080");
}

TEST_CASE("TextTransforms.RotateZ_90deg_NotCut_1080x1920") {
    auto renderer = test::make_renderer();
    auto r = render_rotate_z_ar(renderer, 90.0f, 1080, 1920);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    INFO("90deg-1080x1920 bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 200);
    CHECK_FALSE(bbox.touches_left(0));
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(r.height, 0));
    CHECK_FALSE(bbox.touches_right(r.width, 0));

    verify_rotate_z_golden(*r.fb, "rotate_z_90deg_1080x1920");
}
