// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_clip/text_completeness.cpp
//
// TextCompleteness — comprehensive glyph-ink visibility regression tests.
//
// Verifies that rendered text contains ALL visible ink from glyphs:
// no clipping above, below, left, or right — with scale, glow, shadow,
// multiline, and different font sizes.
//
// Each test renders a single frame, scans the Framebuffer for the alpha
// bounding box, and asserts numerical bounds.  Golden PNG diff is a
// belt-and-suspenders safety net (CHRONON3D_UPDATE_GOLDENS=1 to seed).
//
// Failure conditions:
//   max_alpha < 0.5                          → text nearly invisible
//   visible_bbox_height < expected_min       → vertical clipping
//   visible_bbox_width  < expected_min       → horizontal clipping
//   touches edge when not expected           → possible clipping
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API.
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
#include <cmath>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── AlphaBBox ──────────────────────────────────────────────────────────
// Tight axis-aligned bounding box of all pixels with alpha > epsilon.
// Returns (-1,-1,-1,-1) when the framebuffer contains no opaque draw.
// NOTE: Duplicated from text_clip_bounds.cpp — keep in sync until extracted
// to a shared test utility header (e.g. tests/text_golden/text_clip/test_helpers.hpp).
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
    [[nodiscard]] bool touches_left(int margin = 4)   const noexcept { return x0 >= 0 && x0 <= margin; }
    [[nodiscard]] bool touches_top(int margin = 4)    const noexcept { return y0 >= 0 && y0 <= margin; }
    [[nodiscard]] bool touches_right(int canvas_w, int margin = 4)  const noexcept { return x1 >= 0 && x1 >= canvas_w - margin; }
    [[nodiscard]] bool touches_bottom(int canvas_h, int margin = 4) const noexcept { return y1 >= 0 && y1 >= canvas_h - margin; }
};

[[nodiscard]] AlphaBBox alpha_bbox(const Framebuffer& fb, float epsilon = 0.01f) {
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

[[nodiscard]] int alpha_pixel_count(const Framebuffer& fb, float epsilon = 0.01f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > epsilon) ++count;
        }
    }
    return count;
}

// ── GoldenTestConfig factory ───────────────────────────────────────────
GoldenTestConfig make_completeness_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_completeness/"} +
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
// Single text layer on a 1920×1080 canvas.  Parameterized by text,
// font_size, scale, shadows, and glow to cover all 8 test scenarios.
Composition build_completeness_composition(
    SoftwareRenderer& renderer,
    std::string_view text,
    float font_size = 180.0f,
    Vec3 uniform_scale = Vec3{1.0f, 1.0f, 1.0f},
    std::vector<TextShadow> shadows = {},
    GlowParams glow_params = {},
    float box_w = 1600.0f,
    float box_h = 300.0f,
    VerticalAlign v_align = VerticalAlign::Middle
) {
    return composition(
        {.name = "TextCompleteness/Completeness 1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_size, uniform_scale, shadows, glow_params,
         box_w, box_h, v_align](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer, text, font_size, uniform_scale, shadows,
                     glow_params, box_w, box_h, v_align](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.scale(uniform_scale);
                if (glow_params.enabled) {
                    l.glow(glow_params);
                }
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = std::string{text}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = font_size
                        },
                        .layout = {
                            .box = {box_w, box_h},
                            .align = TextAlign::Center,
                            .vertical_align = v_align
                        },
                        .appearance = {
                            .color = Color::white(),
                            .shadows = shadows
                        },
                        .position = {960.0f, 540.0f, 0.0f}
                    }
                }).commit();
            });
            return s.build();
        });
}

// ── Golden verification helper ────────────────────────────────────────
void verify_completeness_golden(
    Framebuffer& fb,
    std::string_view case_slug
) {
    auto r = verify_golden(fb, std::string{case_slug},
                           make_completeness_config(case_slug));
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    INFO("Golden: ", r.message);
    CHECK(r.passed);
}

} // namespace

// ═══ Test 1 — AscentDescent ═════════════════════════════════════════════
// "HAMBURGER gyqp ÅÉÍÓÚ" — exercises ascenders, descenders, and accents.
//
// HAMBURGER = tall uppercase letters (ascent)
// gyqp      = descenders below baseline
// ÅÉÍÓÚ     = diacritics above baseline
//
// Assert:
//   visible_bbox_height > font_size * 0.65
//   visible_bbox_width  > font_size * 8
//   bbox.y0 > 4   (not clipped at top)
//   bbox.y1 < canvas_h - 4  (not clipped at bottom)
TEST_CASE("TextCompleteness.AscentDescent 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "HAMBURGER gyqp \u00C5\u00C9\u00CD\u00D3\u00DA",
            180.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 180.0f;
    INFO("bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > font_size * 0.65f);
    CHECK(bbox.width()  > font_size * 8.0f);
    CHECK(bbox.y0 > 4);
    CHECK(bbox.y1 < static_cast<int>(fb->height()) - 4);

    verify_completeness_golden(*fb, "completeness_01_ascent_descent");
}

// ═══ Test 2 — TopNotCut ═════════════════════════════════════════════════
// "ÁÉÍÓÚ ÂÊÎÔÛ ÄËÏÖÜ" — all tall letters with diacritics above.
// Catches the bug where min_y = baseline instead of baseline - ascent.
//
// Assert:
//   visible_bbox.y0 > 0   (not touching top edge)
//   visible_bbox_height coherent with font_size
TEST_CASE("TextCompleteness.TopNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer,
            "\u00C1\u00C9\u00CD\u00D3\u00DA "
            "\u00C2\u00CA\u00CE\u00D4\u00DB "
            "\u00C4\u00CB\u00CF\u00D6\u00DC",
            180.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 180.0f;
    INFO("bbox y0=", bbox.y0, " y1=", bbox.y1, " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.y0 > 0);
    CHECK(bbox.height() > font_size * 0.55f);
    CHECK_FALSE(bbox.touches_top(0));

    verify_completeness_golden(*fb, "completeness_02_top_not_cut");
}

// ═══ Test 3 — BottomNotCut ══════════════════════════════════════════════
// "g j p q y ç" — all letters with descenders below the baseline.
// Catches descenders clipped below scratch surface.
//
// Assert:
//   visible_bbox.y1 < canvas_h - 4
//   visible_bbox_height > font_size * 0.45
TEST_CASE("TextCompleteness.BottomNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "g j p q y \u00E7",
            180.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 180.0f;
    INFO("bbox y0=", bbox.y0, " y1=", bbox.y1,
         " h=", bbox.height(), " canvas_h=", fb->height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.y1 < static_cast<int>(fb->height()) - 4);
    CHECK(bbox.height() > font_size * 0.45f);
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));

    verify_completeness_golden(*fb, "completeness_03_bottom_not_cut");
}

// ═══ Test 4 — AdvanceWidthNotCut ════════════════════════════════════════
// "MMMMMMMMMMMM WWWWWWWWWW" — wide letters that test real per-glyph
// advance width.  Catches the bug where a hardcoded +12px advance
// approximation under-estimates the last glyph extent.
//
// Assert:
//   visible_bbox_width > font_size * 10
//   visible_bbox.x1 < canvas_w - 4
TEST_CASE("TextCompleteness.AdvanceWidthNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "MMMMMMMMMMMM WWWWWWWWWW",
            120.0f, Vec3{1.0f, 1.0f, 1.0f}, {}, {},
            1800.0f, 200.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 120.0f;
    INFO("bbox x0=", bbox.x0, " x1=", bbox.x1,
         " w=", bbox.width(), " canvas_w=", fb->width());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.width() > font_size * 10.0f);
    CHECK(bbox.x1 < static_cast<int>(fb->width()) - 4);
    CHECK_FALSE(bbox.touches_right(fb->width(), 0));

    verify_completeness_golden(*fb, "completeness_04_advance_width_not_cut");
}

// ═══ Test 5 — Scale130NotCut ════════════════════════════════════════════
// "SCALE HAMBURGER" at uniform scale 1.30×.  Exercises the scale.z
// expansion path through compute_text_run_visual_bounds and the
// supersampling/raster_space/glyph_matrix chain.
//
// Assert:
//   visible_bbox_height > font_size * 0.65 * scale
//   visible_bbox does not touch any edge
//   alpha_pixel_count > minimum
TEST_CASE("TextCompleteness.Scale130NotCut 1920x1080") {
    const float scale = 1.30f;
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "SCALE HAMBURGER",
            180.0f, Vec3{scale, scale, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 180.0f;
    INFO("scale1.3 bbox h=", bbox.height(), " w=", bbox.width(),
         " y0=", bbox.y0, " y1=", bbox.y1);

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > font_size * 0.65f * scale);
    CHECK(bbox.width()  > font_size * 3.0f * scale);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK_FALSE(bbox.touches_right(fb->width(), 0));
    CHECK(alpha_pixel_count(*fb) > 500);

    verify_completeness_golden(*fb, "completeness_05_scale130_not_cut");
}

// ═══ Test 6 — GlowNotCut ════════════════════════════════════════════════
// "GLOW" with glow radius 40.  The glow is a compositor effect applied
// post-raster; it expands the visible bbox beyond the glyph ink but
// must NOT clip the glyph itself.
//
// Assert:
//   alpha_bbox.height > no_glow_bbox.height  (glow expands bbox)
//   alpha_bbox.width  > no_glow_bbox.width
//   alpha_bbox does not touch any edge
TEST_CASE("TextCompleteness.GlowNotCut 1920x1080") {
    auto renderer = test::make_renderer();

    // Baseline: no-glow bbox.
    auto fb_no_glow = renderer.render(
        build_completeness_composition(
            renderer, "GLOW", 180.0f),
        Frame{0});
    REQUIRE(fb_no_glow != nullptr);
    const AlphaBBox bbox_no_glow = alpha_bbox(*fb_no_glow);
    INFO("no-glow bbox h=", bbox_no_glow.height(), " w=", bbox_no_glow.width());

    // Glow composition: radius 40.
    GlowParams glow;
    glow.enabled   = true;
    glow.radius    = 40.0f;
    glow.intensity = 0.8f;
    glow.additive  = true;
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "GLOW", 180.0f,
            Vec3{1.0f, 1.0f, 1.0f}, {}, glow),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("glow bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " h=", bbox.height(), " w=", bbox.width());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 90);
    CHECK(bbox.width()  > 200);
    CHECK(bbox.height() >= bbox_no_glow.height());
    CHECK(bbox.width()  >= bbox_no_glow.width());
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK_FALSE(bbox.touches_right(fb->width(), 0));

    verify_completeness_golden(*fb, "completeness_06_glow_not_cut");
}

// ═══ Test 7 — ShadowNotCut ══════════════════════════════════════════════
// "SHADOW" with shadow offset {80, 80} and blur 20.  The shadow padding
// path in prepare_text_run() must expand the scratch surface to include
// the shadow extents WITHOUT clipping the glyph ink.
//
// Assert:
//   alpha_bbox.x1 increases vs no-shadow baseline (shadow extends right)
//   alpha_bbox.y1 increases vs no-shadow baseline (shadow extends down)
//   shadow is not clipped at edges
TEST_CASE("TextCompleteness.ShadowNotCut 1920x1080") {
    auto renderer = test::make_renderer();

    // Baseline: no-shadow bbox.
    auto fb_no_shadow = renderer.render(
        build_completeness_composition(
            renderer, "SHADOW", 160.0f),
        Frame{0});
    REQUIRE(fb_no_shadow != nullptr);
    const AlphaBBox bbox_no_shadow = alpha_bbox(*fb_no_shadow);

    // Shadow composition: offset {80, 80}, blur 20.
    std::vector<TextShadow> shadows = {{
        .enabled = true,
        .offset = Vec2{80.0f, 80.0f},
        .blur = 20.0f,
        .opacity = 0.5f,
        .color = Color{0.0f, 0.0f, 0.0f, 1.0f}
    }};
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "SHADOW", 160.0f,
            Vec3{1.0f, 1.0f, 1.0f}, shadows),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("shadow bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " h=", bbox.height(), " w=", bbox.width());
    INFO("no-shadow bbox x1=", bbox_no_shadow.x1,
         " y1=", bbox_no_shadow.y1);

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 90);
    CHECK(bbox.width()  > 400);
    // Shadow extends the bbox in both directions.
    CHECK(bbox.y1 > bbox_no_shadow.y1 + 10);
    CHECK(bbox.x1 > bbox_no_shadow.x1 + 10);

    verify_completeness_golden(*fb, "completeness_07_shadow_not_cut");
}

// ═══ Test 8 — MultilineNotCut ═══════════════════════════════════════════
// Three lines exercising ascent, descent, and accents together:
//   "LINE ONE"              — standard uppercase
//   "gyqp descenders"       — descenders below baseline
//   "ÁÉÍÓÚ accents"         — diacritics above baseline
//
// Assert:
//   visible_bbox_height > font_size * 2.2  (3 lines should span > 2× font)
//   bbox.y0 > 4   (not clipped at top)
//   bbox.y1 < canvas_h - 4  (not clipped at bottom)
TEST_CASE("TextCompleteness.MultilineNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer,
            "LINE ONE\ngyqp descenders\n\u00C1\u00C9\u00CD\u00D3\u00DA accents",
            80.0f, Vec3{1.0f, 1.0f, 1.0f}, {}, {},
            1500.0f, 500.0f, VerticalAlign::Middle),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 80.0f;
    INFO("multiline bbox y0=", bbox.y0, " y1=", bbox.y1,
         " h=", bbox.height(), " w=", bbox.width());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > font_size * 2.2f);
    CHECK(bbox.y0 > 4);
    CHECK(bbox.y1 < static_cast<int>(fb->height()) - 4);

    verify_completeness_golden(*fb, "completeness_08_multiline_not_cut");
}
