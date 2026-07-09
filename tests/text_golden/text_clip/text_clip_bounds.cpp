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
// Five TEST_CASEs lock the regression using a "HAMBURGER" 180 pt
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
// Optional `shadows` exercises the shadow-padding path in
// prepare_text_run() (TICKET-TEXT-CLIP-ASCENT).
// Optional `glow_params` exercises the glow compositor path
// (layer-level glow applied post-raster).
Composition build_clip_composition(
    SoftwareRenderer& renderer,
    Vec3 uniform_scale = Vec3{1.0f, 1.0f, 1.0f},
    std::vector<TextShadow> shadows = {},
    GlowParams glow_params = {}
) {
    return composition(
        {.name = "TextClip/HAMBURGER_centered 1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, uniform_scale, shadows, glow_params](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [&renderer, uniform_scale, shadows, glow_params](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                // IMPORTANT: Do NOT call l.scale() with identity values.
                // Even l.scale({1,1,1}) sets item.transform.any() == true,
                // which changes how the graph builder computes world_matrix
                // and causes text to render at the wrong position.
                const bool is_identity_scale =
                    uniform_scale.x == 1.0f && uniform_scale.y == 1.0f && uniform_scale.z == 1.0f;
                if (!is_identity_scale) {
                    l.scale(uniform_scale);
                }
                if (glow_params.enabled) {
                    l.glow(glow_params);
                }
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
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
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

} // namespace

// ═══ Test 1 — AscentNotCut ══════════════════════════════════════════════
// Pre-fix: visible bbox height ≈ 19 px (ascenders clipped above the
// scratch surface) → fails CHECK > 90 immediately.  Post-fix: 180 pt
// font on 1080-row canvas publishes ~250 px of glyph ink.
TEST_CASE("Clip 01 TextClip AscentNotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_composition(renderer),
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
        build_clip_composition(renderer),
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
// Uniform scale 1.30× applied at the layer level.  The scale triggers
// supersampling (factor > 1) and exercises the raster-space scale path
// in prepare_text_run.
TEST_CASE("Clip 03 TextClip Scale130NotCut 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_composition(renderer, Vec3{1.30f, 1.30f, 1.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("scale1.3 bbox height=", bbox.height(), " width=", bbox.width(),
         " x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);

    // Scaled-up bbox should be visible AND shouldn't extend past the
    // right edge of the framebuffer by more than ~5 px.
    CHECK(bbox.height() > 50);
    CHECK(bbox.width() > 500);
    CHECK(bbox.x1 < fb->width() - 5);
}

// ═══ Test 4 — ShadowNotCut ═══════════════════════════════════════════════
// Drop shadow with offset {20, 40} + blur 30 px.  The shadow padding
// path in prepare_text_run() must expand the scratch surface to include
// the shadow extents (offset + blur + safety padding) WITHOUT clipping
// the glyph ink.
TEST_CASE("Clip 04 TextClip ShadowNotCut 1920x1080") {
    auto renderer = test::make_renderer();

    // Baseline: no-shadow bbox (for comparison).
    auto fb_no_shadow = renderer.render(
        build_clip_composition(renderer),
        Frame{0});
    REQUIRE(fb_no_shadow != nullptr);
    const AlphaBBox bbox_no_shadow = alpha_bbox(*fb_no_shadow);
    INFO("no-shadow bbox y0=", bbox_no_shadow.y0,
         " y1=", bbox_no_shadow.y1);

    // Shadow composition: offset {20, 40}, blur 30.
    std::vector<TextShadow> shadows = {{
        .enabled = true,
        .offset = Vec2{20.0f, 40.0f},
        .blur = 30.0f,
        .opacity = 0.5f,
        .color = Color{0.0f, 0.0f, 0.0f, 1.0f}
    }};
    auto fb = renderer.render(
        build_clip_composition(renderer, Vec3{1.0f, 1.0f, 1.0f}, shadows),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("shadow bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " width=", bbox.width(), " height=", bbox.height());

    // Primary assertions: glyph ink not clipped.
    CHECK(bbox.height() > 90);
    CHECK(bbox.width() > 500);
    CHECK(bbox.x1 < fb->width() - 5);

    // Shadow extends the bbox downward (offset.y = 40, blur = 30 => ~70 px).
    CHECK(bbox.y1 > bbox_no_shadow.y1 + 10);

    auto r = verify_golden(*fb, "text_clip_04_shadow_not_cut",
                           make_clip_config("clip_04"));
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 "
                "to create.");
        return;
    }
    INFO("Golden: ", r.message);
    CHECK(r.passed);
}

// ═══ Test 5 — GlowNotCut ═════════════════════════════════════════════════
// Layer-level glow (radius 24, intensity 0.8, additive).
TEST_CASE("Clip 05 TextClip GlowNotCut 1920x1080") {
    auto renderer = test::make_renderer();

    // Baseline: no-glow bbox (for comparison).
    auto fb_no_glow = renderer.render(
        build_clip_composition(renderer),
        Frame{0});
    REQUIRE(fb_no_glow != nullptr);
    const AlphaBBox bbox_no_glow = alpha_bbox(*fb_no_glow);
    INFO("no-glow bbox height=", bbox_no_glow.height(),
         " width=", bbox_no_glow.width());

    // Glow composition: radius 24, intensity 0.8, additive.
    GlowParams glow;
    glow.enabled   = true;
    glow.radius    = 24.0f;
    glow.intensity = 0.8f;
    glow.additive  = true;
    auto fb = renderer.render(
        build_clip_composition(renderer, Vec3{1.0f, 1.0f, 1.0f}, {}, glow),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("glow bbox x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " width=", bbox.width(), " height=", bbox.height());

    // Primary assertions: glyph ink not clipped.
    CHECK(bbox.height() > 90);
    CHECK(bbox.width() > 500);
    CHECK(bbox.x1 < fb->width() - 5);

    // Glow should expand or preserve bbox.
    CHECK(bbox.height() >= bbox_no_glow.height());
    CHECK(bbox.width()  >= bbox_no_glow.width());

    auto r = verify_golden(*fb, "text_clip_05_glow_not_cut",
                           make_clip_config("clip_05"));
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 "
                "to create.");
        return;
    }
    INFO("Golden: ", r.message);
    CHECK(r.passed);
}

// ═══ Test A — DebugLayout Diagnostic ═════════════════════════════════════
// Original analysis (Test A): render with text_layout_debug=true.
TEST_CASE("Clip 06 TextClip DebugLayout Diagnostic 1920x1080") {
    // Normal render (text_layout_debug = false, default)
    auto renderer_normal = test::make_renderer_shared();
    auto fb_normal = renderer_normal->render(
        build_clip_composition(*renderer_normal),
        Frame{0});
    REQUIRE(fb_normal != nullptr);
    const AlphaBBox bbox_normal = alpha_bbox(*fb_normal);
    INFO("NORMAL  bbox: x0=", bbox_normal.x0, " y0=", bbox_normal.y0,
         " x1=", bbox_normal.x1, " y1=", bbox_normal.y1,
         " w=", bbox_normal.width(), " h=", bbox_normal.height());

    // Debug render (text_layout_debug = true)
    auto renderer_debug = test::make_renderer_shared();
    RenderSettings debug_settings;
    debug_settings.use_modular_graph = true;  // mirror make_renderer_shared
    debug_settings.text_layout_debug = true;
    renderer_debug->set_settings(debug_settings);
    auto fb_debug = renderer_debug->render(
        build_clip_composition(*renderer_debug),
        Frame{0});
    REQUIRE(fb_debug != nullptr);
    const AlphaBBox bbox_debug = alpha_bbox(*fb_debug);
    INFO("DEBUG   bbox: x0=", bbox_debug.x0, " y0=", bbox_debug.y0,
         " x1=", bbox_debug.x1, " y1=", bbox_debug.y1,
         " w=", bbox_debug.width(), " h=", bbox_debug.height());

    // Diagnostic comparison
    INFO("Delta: dy0=", bbox_debug.y0 - bbox_normal.y0,
         " dy1=", bbox_debug.y1 - bbox_normal.y1,
         " dh=", bbox_debug.height() - bbox_normal.height(),
         " dx1=", bbox_debug.x1 - bbox_normal.x1);

    // ── Export PNGs for visual inspection ─────────────────────────────────
    const char* kNormalPath = "/tmp/clip06_normal.png";
    const char* kDebugPath  = "/tmp/clip06_debug.png";
    CHECK(save_png(*fb_normal, kNormalPath));
    CHECK(save_png(*fb_debug,  kDebugPath));
    MESSAGE("PNG exported: ", kNormalPath);
    MESSAGE("PNG exported: ", kDebugPath);

    const bool debug_fixes_clipping =
        bbox_debug.height() > bbox_normal.height() + 50;

    if (debug_fixes_clipping) {
        MESSAGE("VERDICT: debug fixes vertical clipping -> bug is in predicted_bbox / compositor, NOT in scratch surface");
    } else if (bbox_debug.height() <= bbox_normal.height()) {
        MESSAGE("VERDICT: debug does NOT fix vertical clipping -> bug is in scratch raster surface / local bounds");
    } else {
        MESSAGE("Ambiguous: debug height =", bbox_debug.height(),
                "normal height =", bbox_normal.height(),
                "(difference < 50 px threshold — manual investigation needed)");
    }

    // Sanity-check: normal render must have produced visible pixels
    CHECK(bbox_normal.height() > 0);
}
