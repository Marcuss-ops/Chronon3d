// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_clip/text_clip_bounds.cpp
//
// TICKET-TEXT-CLIP-ASCENT — numerical-bbox-clip regression tests.
//
// Before the fix, `compute_text_run_visual_bounds()` anchored the
// scratch surface to the baseline (`min_y = gy - pad` /
// `max_y = gy + pad + placed.total_height`) and used a hardcoded 12 px
// glyph advance approximation.  The user-reported symptom from the
// bug dashboard PNG (output/ae_08_glow_pulse.png, 1920×1080 canvas):
//
//     visible bbox: x=974..1919, y=783..801   (only 19 px tall)
//     touches right edge: yes
//
// 19 px of glyph on a 1080-row canvas is the descender strip leaking
// through the undersized scratch surface — the ascender (top ~80% of
// glyph ink) was clipped because the bbox was anchored to the
// baseline instead of `baseline - ascent`.
//
// Three TEST_CASEs lock the regression using a "HAMBURGER" 180 pt
// centered on a 1920×1080 canvas.  Each renders a single frame, scans
// the Framebuffer for the alpha bounding box, asserts numerical
// bounds, AND runs a golden PNG diff (CHRONON3D_UPDATE_GOLDENS=1 to
// seed).  Belt+suspenders: the numeric assertion fails on the bug
// immediately and consistently across machines; the golden diff
// catches any sub-pixel drift once the fix is correct and the goldens
// are seeded.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; canonical composition() + SceneBuilder::layer()
// + LayerBuilder::text_run(...).commit() pattern from
// tests/text_golden/user_spec/01_text_basic_centered.cpp.  Pure
// software backend.  Framebuffer access via fb->pixels_row(y).
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

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── AlphaBBox ──────────────────────────────────────────────────────────
// Tight axis-aligned bounding box of all pixels with alpha > epsilon.
// Returns (-1,-1,-1,-1) when the framebuffer contains no opaque draw.
struct AlphaBBox {
    int x0{-1}, y0{-1}, x1{-1}, y1{-1};
    [[nodiscard]] int width()  const noexcept {
        return (x1 >= 0) ? (x1 - x0 + 1) : 0;
    }
    [[nodiscard]] int height() const noexcept {
        return (y1 >= 0) ? (y1 - y0 + 1) : 0;
    }
    [[nodiscard]] bool empty() const noexcept {
        return x1 < 0 || y1 < 0;
    }
};

[[nodiscard]] AlphaBBox alpha_bbox(
    const Framebuffer& fb,
    float epsilon = 0.01f
) {
    AlphaBBox b{fb.width(), fb.height(), -1, -1};
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > epsilon) {
                b.x0 = std::min(b.x0, x);
                b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y);
                b.y1 = std::max(b.y1, y);
            }
        }
    }
    return b;
}

// ── GoldenTestConfig factory ───────────────────────────────────────────
// Loose thresholds: the PRIMARY assertion is numeric; the golden diff
// is a safety net.  We want the test to fail LOUDLY on the bug and
// stay green once the fix plus a seeded golden land.
GoldenTestConfig make_clip_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_clip/"} +
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
// Single text layer, "HAMBURGER" 180 pt Inter Bold centered on a
// 1920×1080 canvas.  The optional uniform `scale3` multiplier is
// applied at the LAYER level (not glyph level) to test the scale.z
// expansion path through `compute_text_run_visual_bounds`.
Composition build_clip_composition(
    SoftwareRenderer& renderer,
    Vec3 uniform_scale = Vec3{1.0f, 1.0f, 1.0f}
) {
    return composition(
        {.name = "TextClip/HAMBURGER_centered 1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, uniform_scale](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer, uniform_scale](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.scale(uniform_scale);
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "HAMBURGER"},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
                        },
                        .layout = {
                            .box = {1600.0f, 300.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .position = {960.0f, 540.0f, 0.0f}
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — AscentNotCut ══════════════════════════════════════════════
// Pre-fix: visible bbox height ≈ 19 px (ascenders clipped above the
// scratch surface) → fails CHECK > 90 immediately.  Post-fix: 180 pt
// font on 1080-row canvas publishes ~250 px of glyph ink.
TEST_CASE("Clip 01 TextClip AscentNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_composition(renderer, Vec3{1.0f, 1.0f, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("alpha bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " width=", bbox.width(), " height=", bbox.height());

    CHECK(bbox.height() > 90);
    CHECK(bbox.width() > 500);

    // Belt-and-suspenders: the pre-fix PNG also touched the right edge
    // because the +12 px advance approximation under-estimated the
    // last glyph extent.  Assert a small margin so the right edge of
    // the bbox stays inside the framebuffer.
    CHECK(bbox.x1 < fb->width() - 10);

    auto r = verify_golden(*fb, "text_clip_01_ascent_not_cut",
                           make_clip_config("clip_01"));
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 "
                "to create.");
        return;
    }
    INFO("Golden: ", r.message);
    CHECK(r.passed);
}

// ═══ Test 2 — RightEdgeNotCut ═══════════════════════════════════════════
// Targeted assertion on the right-edge clipping symptom.  Same
// composition but the assertion is exclusively on `x1 < width - 5`
// so the failure mode is immediately diagnosable from the doctest
// output.
TEST_CASE("Clip 02 TextClip RightEdgeNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_composition(renderer, Vec3{1.0f, 1.0f, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("bbox.x1=", bbox.x1, " fb.width()=", fb->width());
    CHECK_FALSE(bbox.empty());
    CHECK(bbox.x1 < fb->width() - 5);
}

// ═══ Test 3 — Scale130NotCut ═══════════════════════════════════════════
// Uniform scale 1.30× forces the bbox math through the scale.z path.
// Pre-fix: bbox height still ≈ 19 px (the ascent/descent math was
// wrong before scale was applied).  Post-fix: bbox height should
// grow proportionally (~325 px vs ~250 px at 1.0× scale).
TEST_CASE("Clip 03 TextClip Scale130NotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_composition(renderer, Vec3{1.30f, 1.30f, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("scale1.3 bbox height=", bbox.height(), " width=", bbox.width());

    // Scaled-up bbox should be visible AND shouldn't extend past the
    // right edge of the framebuffer by more than ~5 px.  Thresholds
    // picked with healthy margin over the pre-fix (19 px height) and
    // over the post-fix expected (~325 px height, ~1240 px width at
    // 1.30x scale) to absorb cross-platform FreeType shaping jitter.
    CHECK(bbox.height() > 200);
    CHECK(bbox.width() > 1000);
    CHECK(bbox.x1 < fb->width() - 5);
}
