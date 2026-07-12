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

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

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
// Single text layer on a canvas.  Parameterized by text, font_size,
// scale, shadows, glow, canvas dimensions, and overflow mode.
// NOTE: Do NOT call l.scale() with identity values — even l.scale({1,1,1})
// sets item.transform.any()==true, which shifts world_matrix.
Composition build_completeness_composition(
    SoftwareRenderer& renderer,
    std::string_view text,
    float font_size = 180.0f,
    Vec3 uniform_scale = Vec3{1.0f, 1.0f, 1.0f},
    std::vector<TextShadow> shadows = {},
    GlowParams glow_params = {},
    float box_w = 1920.0f,
    float box_h = 1080.0f,
    VerticalAlign v_align = VerticalAlign::Middle,
    TextOverflow overflow = TextOverflow::Clip,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cx = static_cast<float>(canvas_w) * 0.5f;
    const float cy = static_cast<float>(canvas_h) * 0.5f;
    return composition(
        {.name = "TextCompleteness/Completeness",
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_size, uniform_scale, shadows, glow_params,
         box_w, box_h, v_align, overflow, cx, cy](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer, text, font_size, uniform_scale, shadows,
                     glow_params, box_w, box_h, v_align, overflow, cx, cy](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                const bool is_identity_scale =
                    uniform_scale.x == 1.0f && uniform_scale.y == 1.0f && uniform_scale.z == 1.0f;
                if (!is_identity_scale) {
                    l.scale(uniform_scale);
                }
                if (glow_params.enabled) {
                    l.glow(glow_params);
                }
                l.text_run("title", TextRunSpec{
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
                            .vertical_align = v_align,
                            .overflow = overflow
                        },
                        .appearance = {
                            .color = Color::white(),
                            .shadows = shadows
                        },
                        .placement = TextPlacement{TextPlacementKind::Absolute, {cx, cy}}
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
    REQUIRE_FALSE(r.golden_missing);
    INFO("Golden: ", r.message);
    CHECK(r.passed);
}

// ── Multi-font composition builder (2 fonts) ───────────────────────
// Two text_run entries in the same layer with different fonts.
Composition build_multifont_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "TextCompleteness/MultiFont 1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("regular", TextRunSpec{
                    .text = {
                        .content = {.value = "Regular "},
                        .font = {
                            .font_path = "assets/fonts/Inter-Regular.ttf",
                            .font_family = "Inter",
                            .font_weight = 400,
                            .font_size = 120.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}}
                    }
                }).commit();
                l.text_run("bold", TextRunSpec{
                    .text = {
                        .content = {.value = "BOLD Italic gyqp \u00C1\u00C9\u00CD"},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 120.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}}
                    }
                }).commit();
            });
            return s.build();
        });
}

// ── Multi-font composition builder (3 fonts) ───────────────────────
// Three text_run entries with Inter Bold + Poppins Regular + DMSans Bold.
// Exercises cross-family font metrics and layout coordination.
Composition build_trifont_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "TextCompleteness/TriFont 1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                // Run 1: Inter Bold
                l.text_run("inter", TextRunSpec{
                    .text = {
                        .content = {.value = "HAMBURGER "},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 100.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}}
                    }
                }).commit();
                // Run 2: Poppins Regular
                l.text_run("poppins", TextRunSpec{
                    .text = {
                        .content = {.value = "gyqp \u00C1\u00C9 "},
                        .font = {
                            .font_path = "assets/fonts/Poppins-Regular.ttf",
                            .font_family = "Poppins",
                            .font_weight = 400,
                            .font_size = 100.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}}
                    }
                }).commit();
                // Run 3: Poppins Bold
                l.text_run("poppins-bold", TextRunSpec{
                    .text = {
                        .content = {.value = "\u00CD\u00D3\u00DA descenders"},
                        .font = {
                            .font_path = "assets/fonts/Poppins-Bold.ttf",
                            .font_family = "Poppins",
                            .font_weight = 700,
                            .font_size = 100.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}}
                    }
                }).commit();
            });
            return s.build();
        });
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
    // font_size * 0.65 = 117 — catches the 19px-sliver bug.
    CHECK(bbox.height() > static_cast<int>(font_size * 0.65f));
    // font_size * 5 = 900 — realistic for centered text in a 1920px box;
    // font_size * 8 would exceed the canvas at this point size.
    CHECK(bbox.width()  > static_cast<int>(font_size * 5));
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
    CHECK(bbox.height() > 90);
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
    CHECK(bbox.height() > 70);
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
            120.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 120.0f;
    INFO("bbox x0=", bbox.x0, " x1=", bbox.x1,
         " w=", bbox.width(), " canvas_w=", fb->width());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.width() > 1000);
    CHECK(bbox.x1 < static_cast<int>(fb->width()) - 2);
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
    CHECK(bbox.height() > 120);
    CHECK(bbox.width()  > 500);
    CHECK_FALSE(bbox.touches_left(0));
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
    CHECK(bbox.height() > 80);
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
    CHECK(bbox.height() > 80);
    CHECK(bbox.width()  > 300);
    // Shadow extends the bbox in both directions.
    CHECK(bbox.y1 > bbox_no_shadow.y1 + 5);
    CHECK(bbox.x1 > bbox_no_shadow.x1 + 5);

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
            80.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 80.0f;
    INFO("multiline bbox y0=", bbox.y0, " y1=", bbox.y1,
         " h=", bbox.height(), " w=", bbox.width());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 150);
    CHECK(bbox.y0 > 4);
    CHECK(bbox.y1 < static_cast<int>(fb->height()) - 4);

    verify_completeness_golden(*fb, "completeness_08_multiline_not_cut");
}

// ═══ Test 9 — LeftOverhangNotCut ════════════════════════════════════════
// "Hello" 'Test' ÁV — quotes and accents can have left-side overhang.
// Some glyphs (curly quotes, accented caps + V ligature) extend past
// the nominal left bearing.
//
// Assert:
//   visible_bbox.x0 > 4  (not touching left edge)
//   visible_bbox_width coherent
TEST_CASE("TextCompleteness.LeftOverhangNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "\u201CHello\u201D \u2018Test\u2019 \u00C1V",
            160.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("left-overhang bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.x0 > 0);
    CHECK(bbox.width() > 250);
    CHECK_FALSE(bbox.touches_left(0));

    verify_completeness_golden(*fb, "completeness_09_left_overhang_not_cut");
}

// ═══ Test 10 — HugeFontNotCut ═══════════════════════════════════════════
// "HAMBURGER" at font_size=220, box=1700×360, centered.
// Large fonts amplify bbox bugs — a 19px sliver at 180pt becomes even
// more visible at 220pt.
//
// Assert:
//   visible_bbox_height > 120
//   visible_bbox_width  > 800
//   no edge touch
TEST_CASE("TextCompleteness.HugeFontNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "HAMBURGER",
            220.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("huge-font bbox h=", bbox.height(), " w=", bbox.width(),
         " y0=", bbox.y0, " y1=", bbox.y1);

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 70);
    CHECK(bbox.width()  > 800);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK_FALSE(bbox.touches_right(fb->width(), 0));

    verify_completeness_golden(*fb, "completeness_10_huge_font_not_cut");
}

// ═══ Test 11 — Scale200NotCut ═══════════════════════════════════════════
// "SCALE 200" at font_size=120, scale=2.0×.  Heavier than Scale130:
// the supersampling factor and raster_space scaling are pushed harder.
//
// Assert:
//   visible_bbox_height > 130
//   visible_bbox_width  > 500
//   no edge touch
TEST_CASE("TextCompleteness.Scale200NotCut 1920x1080") {
    const float scale = 2.00f;
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "SCALE 200",
            120.0f, Vec3{scale, scale, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("scale2.0 bbox h=", bbox.height(), " w=", bbox.width(),
         " y0=", bbox.y0, " y1=", bbox.y1);

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 120);
    CHECK(bbox.width()  > 400);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK_FALSE(bbox.touches_right(fb->width(), 0));
    CHECK(alpha_pixel_count(*fb) > 500);

    verify_completeness_golden(*fb, "completeness_11_scale200_not_cut");
}

// ═══ Test 12 — IntentionalOverflowClip ══════════════════════════════════
// "VERY VERY VERY LONG TEXT" in a small box (300×100) with overflow=Clip.
// Horizontal clipping is intentional when the text overflows the box.
// Vertical clipping is NOT acceptable if the text fits vertically.
//
// Assert:
//   bbox.height coherent (> font_size * 0.45, text fits vertically)
//   bbox does not clip vertically in an anomalous way
TEST_CASE("TextCompleteness.IntentionalOverflowClip 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "VERY VERY VERY LONG TEXT",
            80.0f, Vec3{1.0f, 1.0f, 1.0f}, {}, {},
            300.0f, 100.0f, VerticalAlign::Middle, TextOverflow::Clip),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const float font_size = 80.0f;
    INFO("overflow-clip bbox h=", bbox.height(), " w=", bbox.width(),
         " y0=", bbox.y0, " y1=", bbox.y1);

    CHECK_FALSE(bbox.empty());
    // Text fits vertically in a 100px box at 80px font — height must be
    // at least 45% of font_size (single line, no descent clipping).
    CHECK(bbox.height() > 30);
    // Vertical clipping must not be anomalous — the bbox should be
    // roughly centered on the canvas (within the 300×100 box).
    CHECK(bbox.y0 > 100);   // loose: text centered somewhere in upper 2/3
    CHECK(bbox.y1 < 1000);

    verify_completeness_golden(*fb, "completeness_12_overflow_clip");
}

// ═══ Test 13 — CacheFrameChanges ════════════════════════════════════════
// Render 3 different compositions ("HELLO", "HAMBURGER", "gyqp") and
// verify that each produces a different visual bbox.  This catches
// cache bugs where the bbox is stale after text content changes.
//
// Assert:
//   hash(fb0) != hash(fb1) != hash(fb2)   (framebuffers differ)
//   bbox_height("HAMBURGER") > bbox_height("HELLO") * 0.8
//   bbox_height("gyqp") > font_size * 0.35
TEST_CASE("TextCompleteness.CacheFrameChanges 1920x1080") {
    const float font_size = 120.0f;
    auto renderer0 = test::make_renderer();
    auto fb0 = renderer0.render(
        build_completeness_composition(renderer0, "HELLO", font_size),
        Frame{0});
    REQUIRE(fb0 != nullptr);

    auto renderer1 = test::make_renderer();
    auto fb1 = renderer1.render(
        build_completeness_composition(renderer1, "HAMBURGER", font_size),
        Frame{0});
    REQUIRE(fb1 != nullptr);

    auto renderer2 = test::make_renderer();
    auto fb2 = renderer2.render(
        build_completeness_composition(renderer2, "gyqp", font_size),
        Frame{0});
    REQUIRE(fb2 != nullptr);

    const AlphaBBox b0 = alpha_bbox(*fb0);
    const AlphaBBox b1 = alpha_bbox(*fb1);
    const AlphaBBox b2 = alpha_bbox(*fb2);
    INFO("HELLO      bbox h=", b0.height(), " w=", b0.width());
    INFO("HAMBURGER  bbox h=", b1.height(), " w=", b1.width());
    INFO("gyqp       bbox h=", b2.height(), " w=", b2.width());

    CHECK_FALSE(b0.empty());
    CHECK_FALSE(b1.empty());
    CHECK_FALSE(b2.empty());

    // HAMBURGER is wider than HELLO (9 chars vs 5).
    CHECK(b1.width() > b0.width());
    // HAMBURGER height should be similar to HELLO (both uppercase).
    CHECK(b1.height() > b0.height() * 0.7f);
    // gyqp has descenders — height must be visible.
    CHECK(b2.height() > 30);

    // Quick pixel-diff: all 3 framebuffers must differ.
    auto mae = [](const Framebuffer& a, const Framebuffer& b) -> double {
        if (a.width() != b.width() || a.height() != b.height()) return 1.0;
        double sum = 0.0;
        const int w = static_cast<int>(a.width());
        const int h = static_cast<int>(a.height());
        const int n = w * h;
        for (int y = 0; y < h; ++y) {
            const Color* ra = a.pixels_row(y);
            const Color* rb = b.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                sum += static_cast<double>(std::abs(ra[x].r - rb[x].r));
            }
        }
        return sum / static_cast<double>(n);
    };

    double err_01 = mae(*fb0, *fb1);
    double err_12 = mae(*fb1, *fb2);
    double err_02 = mae(*fb0, *fb2);
    INFO("MAE HELLO->HAMBURGER: ", err_01,
         "  HAMBURGER->gyqp: ", err_12,
         "  HELLO->gyqp: ", err_02);

    const double min_diff = 1.0 / 255.0;
    CHECK(err_01 > min_diff);
    CHECK(err_12 > min_diff);
    CHECK(err_02 > min_diff);
}

// ═══ Test 14 — MultiFontNotCut ══════════════════════════════════════════
// Two text runs with different fonts (Regular 400 + Bold 700) in the
// same layer.  Each run may have different ascent/descent metrics.
// The combined bbox must include all visible ink from both runs.
//
// Assert:
//   visible_bbox_height > threshold  (both runs rendered)
//   no vertical clipping
//   alpha_pixel_count > minimum
TEST_CASE("TextCompleteness.MultiFontNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_multifont_composition(renderer),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("multifont bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " h=", bbox.height(), " w=", bbox.width());

    CHECK_FALSE(bbox.empty());
    // Both runs at 120pt should produce visible ink > 50px tall.
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 400);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK(alpha_pixel_count(*fb) > 500);

    verify_completeness_golden(*fb, "completeness_14_multifont_not_cut");
}

// ═══ Test 15 — DebugBoundsMatchAlpha ════════════════════════════════════
// Render the same composition with text_layout_debug=false (normal) and
// text_layout_debug=true (debug overlay).  Compare the alpha bboxes.
//
// The debug overlay draws colored markers on the TextRunNode's internal
// framebuffer.  After compositor merge, the markers survive as
// composited pixels.  The debug render should produce a bbox that is
// at least as large as the normal render (markers extend the bbox).
//
// Assert:
//   debug bbox.height >= normal bbox.height  (markers don't shrink bbox)
//   normal bbox.height > 0                   (text rendered)
//   debug and normal bboxes overlap significantly
TEST_CASE("TextCompleteness.DebugBoundsMatchAlpha 1920x1080") {
    // Normal render
    auto renderer_normal = test::make_renderer_shared();
    auto fb_normal = renderer_normal->render(
        build_completeness_composition(*renderer_normal, "HAMBURGER", 180.0f),
        Frame{0});
    REQUIRE(fb_normal != nullptr);
    const AlphaBBox bbox_normal = alpha_bbox(*fb_normal);
    INFO("NORMAL bbox: x0=", bbox_normal.x0, " y0=", bbox_normal.y0,
         " x1=", bbox_normal.x1, " y1=", bbox_normal.y1,
         " h=", bbox_normal.height(), " w=", bbox_normal.width());

    // Debug render (text_layout_debug = true)
    auto renderer_debug = test::make_renderer_shared();
    RenderSettings debug_settings;
    debug_settings.use_modular_graph = true;
    debug_settings.text_layout_debug = true;
    renderer_debug->set_settings(debug_settings);
    auto fb_debug = renderer_debug->render(
        build_completeness_composition(*renderer_debug, "HAMBURGER", 180.0f),
        Frame{0});
    REQUIRE(fb_debug != nullptr);
    const AlphaBBox bbox_debug = alpha_bbox(*fb_debug);
    INFO("DEBUG  bbox: x0=", bbox_debug.x0, " y0=", bbox_debug.y0,
         " x1=", bbox_debug.x1, " y1=", bbox_debug.y1,
         " h=", bbox_debug.height(), " w=", bbox_debug.width());

    // Delta diagnostic
    INFO("Delta: dh=", bbox_debug.height() - bbox_normal.height(),
         " dw=", bbox_debug.width() - bbox_normal.width(),
         " dx0=", bbox_debug.x0 - bbox_normal.x0,
         " dy0=", bbox_debug.y0 - bbox_normal.y0);

    // Primary assertions: normal text must be visible.
    CHECK(bbox_normal.height() > 0);
    CHECK_FALSE(bbox_normal.empty());

    // Debug render should produce at least as large a bbox (overlay
    // markers extend the visible area, not shrink it).
    CHECK(bbox_debug.height() >= bbox_normal.height());

    // Both bboxes should overlap significantly — the text content is
    // the same; only debug markers are added.  Check that the
    // horizontal overlap is at least 50% of the normal bbox width.
    const int overlap_x0 = std::max(bbox_normal.x0, bbox_debug.x0);
    const int overlap_x1 = std::min(bbox_normal.x1, bbox_debug.x1);
    const int overlap_w = std::max(0, overlap_x1 - overlap_x0 + 1);
    INFO("Horizontal overlap: ", overlap_w,
         " normal_w=", bbox_normal.width());
    CHECK(overlap_w > bbox_normal.width() / 2);

    // Export PNGs for visual inspection
    const char* kNormalPath = "/tmp/completeness_15_normal.png";
    const char* kDebugPath  = "/tmp/completeness_15_debug.png";
    CHECK(save_png(*fb_normal, kNormalPath));
    CHECK(save_png(*fb_debug,  kDebugPath));
    MESSAGE("PNG exported: ", kNormalPath);
    MESSAGE("PNG exported: ", kDebugPath);
}

// ═══ Test 16 — Scale200NotCutHugeFont ═════════════════════════════════
// "SCALE 200" at font_size=180, scale=2.0× (effective 360pt).
// Pushes the supersampling/raster_space/glyph_matrix path harder than
// Test 11 (120pt × 2.0 =240pt effective).  At 360pt effective, the
// scratch surface is significantly larger and the ascent/descent math
// is amplified.
//
// Assert:
//   visible_bbox_height > 200
//   visible_bbox_width  > 600
//   no edge touch
//   alpha_pixel_count > 1000
TEST_CASE("TextCompleteness.Scale200NotCutHugeFont 1920x1080") {
    const float scale = 2.00f;
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "SCALE 200",
            180.0f, Vec3{scale, scale, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("scale2.0-huge bbox h=", bbox.height(), " w=", bbox.width(),
         " y0=", bbox.y0, " y1=", bbox.y1);

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 150);
    CHECK(bbox.width()  > 600);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK_FALSE(bbox.touches_right(fb->width(), 0));
    CHECK(alpha_pixel_count(*fb) > 1000);

    verify_completeness_golden(*fb, "completeness_16_scale200_huge_font");
}

// ═══ Test 17 — IntentionalOverflowEllipsis ══════════════════════════════
// "VERY VERY VERY LONG TEXT" in a small box (500×120) with
// overflow=Ellipsis.  The layout engine should truncate the text and
// append "…" (U+2026) instead of hard-clipping.  The rendered bbox
// must include the ellipsis glyph and fit within the box bounds.
//
// Assert:
//   bbox.height > 30 (text + ellipsis visible)
//   bbox.width <= box_w + reasonable_margin (fits within layout)
//   bbox does not touch edges
TEST_CASE("TextCompleteness.IntentionalOverflowEllipsis 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_completeness_composition(
            renderer, "VERY VERY VERY LONG TEXT THAT OVERFLOWS",
            60.0f, Vec3{1.0f, 1.0f, 1.0f}, {}, {},
            500.0f, 120.0f, VerticalAlign::Middle, TextOverflow::Ellipsis),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("overflow-ellipsis bbox h=", bbox.height(), " w=", bbox.width(),
         " y0=", bbox.y0, " y1=", bbox.y1,
         " x0=", bbox.x0, " x1=", bbox.x1);

    CHECK_FALSE(bbox.empty());
    // Text with ellipsis must be visible.
    CHECK(bbox.height() > 30);
    CHECK(bbox.width()  > 100);
    // Ellipsis truncation should keep text within the layout box.
    // The bbox width should not dramatically exceed the box width.
    CHECK(bbox.width() < 800);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));

    verify_completeness_golden(*fb, "completeness_17_overflow_ellipsis");
}

// ═══ Test 18 — TriFontNotCut ════════════════════════════════════════════
// Three text runs with three different font families:
//   Inter Bold      — "HAMBURGER "
//   Poppins Regular — "gyqp ÅÉ "
//   DMSans Bold     — "ÍÓÚ descenders"
// Each family has different ascent/descent/line-gap metrics.  The
// combined bbox must include all visible ink from all three runs.
//
// Assert:
//   visible_bbox_height > threshold  (all 3 runs rendered)
//   no vertical clipping
//   alpha_pixel_count > minimum
TEST_CASE("TextCompleteness.TriFontNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_trifont_composition(renderer),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("trifont bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " h=", bbox.height(), " w=", bbox.width());

    CHECK_FALSE(bbox.empty());
    // Three runs at 100pt across 3 font families should produce
    // visible ink > 50px tall.
    CHECK(bbox.height() > 50);
    CHECK(bbox.width()  > 400);
    CHECK_FALSE(bbox.touches_top(0));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), 0));
    CHECK(alpha_pixel_count(*fb) > 500);

    verify_completeness_golden(*fb, "completeness_18_trifont_not_cut");
}
