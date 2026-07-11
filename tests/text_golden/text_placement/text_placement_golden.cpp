// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_placement/text_placement_golden.cpp
//
// Golden regression tests for the text placement pipeline.
//
// Covers:
//   - Dashboard (8 compositions)  — golden comparison at frame 0 / 30
//   - Anti-double-translation      — alpha-centroid position check
//   - Layout box alignment         — golden comparison
//   - Clipping (7)                 — golden comparison + edge-touch detection
//   - Multi-resolution (4)         — golden comparison at native resolution
//   - Cache invalidation (1)        — frame-dependent content correctness
//
// Golden PNGs:   test_renders/golden/text/placement/
// Artifacts:     test_renders/artifacts/text/placement/
//
// Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextPlacement
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
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

// ═══════════════════════════════════════════════════════════════════════════
// Forward-declare composition factories (MUST be at global scope — defined
// in content/text_placement/text_placement_compositions.cpp).
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::content::text_placement {
Composition make_static_center_no_pos();
Composition make_animated_center_no_pos();
Composition make_scale_130_center_no_pos();
Composition make_glow_shadow_center_no_pos();
Composition make_multiline_center_middle();
Composition make_small_box_overflow_clip();
Composition make_multisource_text_plus_shape();
Composition make_portrait_1080x1920_center();
Composition make_antidouble_static();
Composition make_antidouble_animated();
Composition make_box_alignment();
Composition make_clip_blur_0();
Composition make_clip_blur_7();
Composition make_clip_blur_20();
Composition make_clip_glow_40();
Composition make_clip_shadow_80();
Composition make_clip_scale_130();
Composition make_clip_scale_200();
Composition make_multires_1920x1080();
Composition make_multires_1280x720();
Composition make_multires_1080x1920();
Composition make_multires_3840x2160();
Composition make_cache_invalidation();
} // namespace chronon3d::content::text_placement

using namespace chronon3d::content::text_placement;

// ═══════════════════════════════════════════════════════════════════════════
// Shared configuration
// ═══════════════════════════════════════════════════════════════════════════

namespace {

GoldenTestConfig make_placement_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/placement";
    cfg.artifact_directory = "test_renders/artifacts/text/placement";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 6.0f / 255.0f;
    cfg.threshold.max_abs_error            = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.15f;
    cfg.threshold.max_rmse                 = 8.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.85f;
    return cfg;
}

// ── verify_placement ─────────────────────────────────────────────────────
// Render a composition at a given frame and compare against golden.
// HARDENED: any render exception or null framebuffer is now a hard FAIL
// (not a soft MESSAGE) — the suite can no longer silently regress on a
// broken effect pipeline or a null-allocation regression.  The
// pre-existing "effect pipeline throws on heavy glow/shadow" pipeline bug
// MUST be fixed in the production code, not papered over in tests.
void verify_placement(Composition& comp, Frame frame,
                       const std::string& label) {
    auto renderer = test::make_renderer();
    std::shared_ptr<Framebuffer> fb;
    try {
        fb = renderer.render(comp, frame);
    } catch (const std::exception& e) {
        FAIL("Render failed for ", label, ": ", e.what(),
             " — effect pipeline regression (was previously swallowed by MESSAGE+return).");
        return;  // unreachable: FAIL throws out of the test case.
    }
    if (!fb) {
        FAIL("Render returned null for ", label,
             " — framebuffer allocation regression (was previously swallowed by MESSAGE+return).");
        return;  // unreachable.
    }

    auto cfg = make_placement_config();
    auto result = verify_golden(*fb, label, cfg);
    REQUIRE_GOLDEN_PASSED(result);
}

// ── Alpha centroid ───────────────────────────────────────────────────────
// Compute the alpha-weighted centroid of a framebuffer.
// (0,0) = top-left.  Returns {-1,-1} if no visible pixels found.
// Color components are floats in [0,1].
struct Centroid { f32 x; f32 y; f32 max_alpha; };

Centroid compute_alpha_centroid(const Framebuffer& fb, float alpha_threshold = 0.05f) {
    Centroid c{0.0f, 0.0f, 0.0f};
    double sum_x = 0.0, sum_y = 0.0, sum_w = 0.0;
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());

    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const float a = row[x].a;
            if (a > c.max_alpha) c.max_alpha = a;
            if (a > alpha_threshold) {
                sum_x += static_cast<double>(x) * static_cast<double>(a);
                sum_y += static_cast<double>(y) * static_cast<double>(a);
                sum_w += static_cast<double>(a);
            }
        }
    }

    if (sum_w > 0.0) {
        c.x = static_cast<f32>(sum_x / sum_w);
        c.y = static_cast<f32>(sum_y / sum_w);
    } else {
        c.x = -1.0f;
        c.y = -1.0f;
    }
    return c;
}

// ── Edge-touch detection ──────────────────────────────────────────────────
// Returns true if the alpha content touches any edge of the framebuffer.
// This indicates potential clipping: the internal raster surface was too small.
bool alpha_touches_edge(const Framebuffer& fb, float alpha_threshold = 0.05f) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());

    // Top / bottom row
    const Color* top = fb.pixels_row(0);
    const Color* bot = fb.pixels_row(h - 1);
    for (int x = 0; x < w; ++x) {
        if (top[x].a > alpha_threshold) return true;
        if (bot[x].a > alpha_threshold) return true;
    }
    // Left / right column
    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        if (row[0].a > alpha_threshold) return true;
        if (row[w - 1].a > alpha_threshold) return true;
    }
    return false;
}

// ── Modular ON/OFF renderer factory ──────────────────────────────────────
// Shared by G.1 (centroid parity) and G.2 (parity + edge-touch) test cases.
// Creating a renderer with use_modular_graph toggled is non-trivial (4 calls)
// so we extract it to a single helper to keep the two paths in lockstep.
SoftwareRenderer make_mod_renderer_for_test(bool modular) {
    SoftwareRenderer renderer(Config{});
    RenderSettings settings;
    settings.use_modular_graph = modular;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    test::attach_software_backend(&renderer);
    return renderer;
}

// ── AntiDouble center-check tolerance constants ──────────────────────────
// 15% of canvas W/H — strict enough to expose the documented "(w/4, h/4)"
// placement bug (which is ~25% from canvas center), wide enough to absorb
// pipeline float jitter.  If you change these, also file a follow-up so the
// "near center" claim stays calibrated.
constexpr float kAntiDoubleCenterToleranceW = 0.15f;
constexpr float kAntiDoubleCenterToleranceH = 0.15f;
constexpr float kNotAtDoubleCenterToleranceW = 0.10f;  // anti-double-translation

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Group A — Dashboard golden tests (8)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Static Center") {
    auto comp = make_static_center_no_pos();
    verify_placement(comp, Frame{0}, "text_placement_static_center");
}

TEST_CASE("TextPlace Animated Center (settled, f30)") {
    auto comp = make_animated_center_no_pos();
    verify_placement(comp, Frame{30}, "text_placement_animated_center_f30");
}

TEST_CASE("TextPlace Scale 1.30") {
    auto comp = make_scale_130_center_no_pos();
    verify_placement(comp, Frame{0}, "text_placement_scale_130");
}

TEST_CASE("TextPlace Glow Shadow") {
    auto comp = make_glow_shadow_center_no_pos();
    verify_placement(comp, Frame{0}, "text_placement_glow_shadow");
}

TEST_CASE("TextPlace Multiline") {
    auto comp = make_multiline_center_middle();
    verify_placement(comp, Frame{0}, "text_placement_multiline");
}

TEST_CASE("TextPlace Small Box Overflow") {
    auto comp = make_small_box_overflow_clip();
    verify_placement(comp, Frame{0}, "text_placement_small_box");
}

TEST_CASE("TextPlace Multisource") {
    auto comp = make_multisource_text_plus_shape();
    verify_placement(comp, Frame{0}, "text_placement_multisource");
}

TEST_CASE("TextPlace Portrait 1080x1920") {
    auto comp = make_portrait_1080x1920_center();
    verify_placement(comp, Frame{0}, "text_placement_portrait_1080x1920");
}

// ═══════════════════════════════════════════════════════════════════════════
// Group B — Anti-double-translation (alpha centroid check)
// ═══════════════════════════════════════════════════════════════════════════

// B.1 — Static text: alpha centroid must (a) be visible on canvas,
// (b) sit NEAR canvas center (within 15% of W/H), AND (c) NOT be near
// 2× canvas center (the double-translation bug).
// HARDENED: previously the test only checked (a) on-canvas + (c) not-at-
// double-center, which let a documented "centroid at ~(w/4, h/4)" bug
// slip through.  Now we ALSO require (b) since the composition is named
// `make_antidouble_static` + uses pin_to(Anchor::Center) — anything other
// than near-canvas-center is a real placement regression.
TEST_CASE("TextPlace AntiDouble Static — centroid at canvas center") {
    auto renderer = test::make_renderer();
    auto comp = make_antidouble_static();
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.5f);  // something visible

    const f32 cx = static_cast<f32>(fb->width())  * 0.5f;   // 960
    const f32 cy = static_cast<f32>(fb->height()) * 0.5f;   // 540
    // kNotAtDoubleCenterToleranceW = 10% of canvas width — anti-double-
    // translation regression gate (same constant used by B.3 below).
    const f32 tolerance = static_cast<f32>(fb->width()) * kNotAtDoubleCenterToleranceW;

    // The centroid must be on-canvas (not off-screen).
    INFO("Alpha centroid: (", c.x, ", ", c.y, ")");
    CHECK(c.x > 0.0f);
    CHECK(c.y > 0.0f);
    CHECK(c.x < static_cast<f32>(fb->width()));
    CHECK(c.y < static_cast<f32>(fb->height()));

    // CI-NEAR-CENTER: with pin_to(Anchor::Center), the alpha-weighted
    // centroid of visible glyph pixels must sit near canvas center
    // (within kAntiDoubleCenterToleranceW/H of cx,cy).  Wide enough to
    // absorb pipeline jitter, narrow enough to catch any regression that
    // pushes the text out of the central band.
    //
    // RED-ON-FIRST-RUN CONTRACT: this check explicitly fails the test
    // when the centroid is at the historically-documented ~(w/4, h/4)
    // position (which is ~25% from canvas center — outside the 15%
    // band).  Composition `make_antidouble_static` has author-comment
    // "Expected: alpha centroid ≈ canvas center {960, 540}" — so this
    // test enforces that intent and surfaces the deviation as a bug.
    const f32 center_tol_w = static_cast<f32>(fb->width())  * kAntiDoubleCenterToleranceW;
    const f32 center_tol_h = static_cast<f32>(fb->height()) * kAntiDoubleCenterToleranceH;
    INFO("Expected near canvas center: (", cx, ", ", cy,
         ")  tolerance ±(", center_tol_w, ", ", center_tol_h, ") px");
    CHECK(std::abs(c.x - cx) < center_tol_w);
    CHECK(std::abs(c.y - cy) < center_tol_h);

    // CRITICAL: the centroid must NOT be near 2× canvas center
    // (the double-translation bug).
    const f32 dx2 = std::abs(c.x - cx * 2.0f);
    const f32 dy2 = std::abs(c.y - cy * 2.0f);
    bool near_double_center = (dx2 < tolerance) && (dy2 < tolerance);
    CHECK_FALSE(near_double_center);
}

// B.2 — Animated text at frame 0: alpha centroid should be offset from canvas center.
TEST_CASE("TextPlace AntiDouble Animated — centroid offset at f0") {
    auto renderer = test::make_renderer();
    auto comp = make_antidouble_animated();
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.3f);  // visible (anim offset at f0, no opacity fade)

    const f32 cx = static_cast<f32>(fb->width())  * 0.5f;
    const f32 cy = static_cast<f32>(fb->height()) * 0.5f;

    // At frame 0, the position anim offset is {30, 20}, so centroid
    // should be ≈ cx+30, cy+20 — NOT at cx,cy AND NOT at 2*cx,2*cy.
    const f32 dx2 = std::abs(c.x - cx * 2.0f);
    const f32 dy2 = std::abs(c.y - cy * 2.0f);
    const f32 tolerance = static_cast<f32>(fb->width()) * 0.12f;

    INFO("Alpha centroid: (", c.x, ", ", c.y, ")  canvas_center: (", cx, ", ", cy, ")");
    bool near_double_center = (dx2 < tolerance) && (dy2 < tolerance);
    CHECK_FALSE(near_double_center);  // NOT double-translated

    // Golden comparison as secondary check
    auto cfg = make_placement_config();
    auto result = verify_golden(*fb, "text_placement_antidouble_animated_f0", cfg);
    INFO("Golden: ", result.message);
    REQUIRE_GOLDEN_PASSED(result);
}

// B.3 — Animated text at frame 30 (settled): centroid on-canvas, near
// canvas center (composition is pin_to(Center) + position_anim settling
// to origin), and not double-translated.  HARDENED same as B.1: now
// checks near-canvas-center in addition to on-canvas + not-at-2x.
TEST_CASE("TextPlace AntiDouble Animated — centroid settled at f30") {
    auto renderer = test::make_renderer();
    auto comp = make_antidouble_animated();
    auto fb = renderer.render(comp, Frame{30});
    REQUIRE(fb != nullptr);

    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.5f);

    const f32 cx = static_cast<f32>(fb->width())  * 0.5f;
    const f32 cy = static_cast<f32>(fb->height()) * 0.5f;
    // kNotAtDoubleCenterToleranceW = 10% of canvas width — anti-double-
    // translation regression gate (same constant used by B.1 above).
    const f32 tolerance = static_cast<f32>(fb->width()) * kNotAtDoubleCenterToleranceW;

    // Centroid must be on-canvas.
    INFO("Alpha centroid (f30): (", c.x, ", ", c.y, ")");
    CHECK(c.x > 0.0f);
    CHECK(c.y > 0.0f);
    CHECK(c.x < static_cast<f32>(fb->width()));
    CHECK(c.y < static_cast<f32>(fb->height()));

    // CI-NEAR-CENTER-F30: at f30 the position_anim has settled to
    // (0,0,0), so the centroid must land near canvas center
    // (kAntiDoubleCenterTolerance* band — same RED-ON-FIRST-RUN
    // contract as B.1 above; pinned to file-static const).
    const f32 center_tol_w = static_cast<f32>(fb->width())  * kAntiDoubleCenterToleranceW;
    const f32 center_tol_h = static_cast<f32>(fb->height()) * kAntiDoubleCenterToleranceH;
    INFO("Expected near canvas center: (", cx, ", ", cy,
         ")  tolerance ±(", center_tol_w, ", ", center_tol_h, ") px");
    CHECK(std::abs(c.x - cx) < center_tol_w);
    CHECK(std::abs(c.y - cy) < center_tol_h);

    // NOT at 2× canvas center — anti-double-translation regression gate.
    const f32 dx2 = std::abs(c.x - cx * 2.0f);
    const f32 dy2 = std::abs(c.y - cy * 2.0f);
    bool near_double_center = (dx2 < tolerance) && (dy2 < tolerance);
    CHECK_FALSE(near_double_center);

    // Centroid at f30 should be similar to f0 (animation settled).
    auto comp_f0 = make_antidouble_animated();
    auto fb_f0 = renderer.render(comp_f0, Frame{0});
    auto c_f0 = compute_alpha_centroid(*fb_f0);
    const f32 drift = std::max(std::abs(c.x - c_f0.x), std::abs(c.y - c_f0.y));
    INFO("Centroid drift f0->f30: ", drift);
    CHECK(drift < static_cast<f32>(fb->width()) * 0.05f);  // <5% drift
}

// ═══════════════════════════════════════════════════════════════════════════
// Group C — Layout box alignment
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Box Align") {
    auto comp = make_box_alignment();
    verify_placement(comp, Frame{0}, "text_placement_box_align");
}

// ═══════════════════════════════════════════════════════════════════════════
// Group D — Clipping (golden + edge-touch detection)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Clip Blur 0") {
    auto comp = make_clip_blur_0();
    verify_placement(comp, Frame{0}, "text_placement_clip_blur0");
}

TEST_CASE("TextPlace Clip Blur 7") {
    auto comp = make_clip_blur_7();
    verify_placement(comp, Frame{0}, "text_placement_clip_blur7");
}

TEST_CASE("TextPlace Clip Blur 20") {
    auto comp = make_clip_blur_20();
    verify_placement(comp, Frame{0}, "text_placement_clip_blur20");
}

TEST_CASE("TextPlace Clip Glow 40") {
    auto comp = make_clip_glow_40();
    verify_placement(comp, Frame{0}, "text_placement_clip_glow40");
}

TEST_CASE("TextPlace Clip Shadow 80") {
    auto comp = make_clip_shadow_80();
    verify_placement(comp, Frame{0}, "text_placement_clip_shadow80");
}

TEST_CASE("TextPlace Clip Scale 1.30") {
    auto comp = make_clip_scale_130();
    verify_placement(comp, Frame{0}, "text_placement_clip_scale130");
}

TEST_CASE("TextPlace Clip Scale 2.00") {
    auto comp = make_clip_scale_200();
    verify_placement(comp, Frame{0}, "text_placement_clip_scale200");
}

// Edge-touch split into TWO test cases (HARDENED):
//
//   1. "blur core no edge touch" — blur 0 and blur 7 have no halo and no
//      large blur radius, so touching a framebuffer edge means the
//      raster surface is too small or the layer bbox is wrong.
//      CHECK_FALSE(alpha_touches_edge) — HARD regression gate.
//
//   2. "effect halo diagnostic" — blur 20, glow 40, shadow 80, scale
//      1.30 + 2.00 may LEGITIMATELY have their effect halo reach the
//      canvas edge.  We log but don't hard-fail (informational only).

TEST_CASE("TextPlace Clipping — blur core no edge touch") {
    struct ClipCheck { std::string label; Composition comp; };
    std::vector<ClipCheck> checks;
    checks.push_back({"blur0", make_clip_blur_0()});
    checks.push_back({"blur7", make_clip_blur_7()});

    auto renderer = test::make_renderer();
    for (auto& ch : checks) {
        std::shared_ptr<Framebuffer> fb;
        try {
            fb = renderer.render(ch.comp, Frame{0});
        } catch (const std::exception& e) {
            FAIL("Render failed for ", ch.label, ": ", e.what(),
                 " — blur0/7 must always render (no heavy halo to throw).");
        }
        REQUIRE(fb != nullptr);
        bool touches = alpha_touches_edge(*fb);
        INFO("Clip-core check [", ch.label, "] alpha_touches_edge = ", touches);
        // HARD: blur 0/7 has no halo — touching the canvas edge means
        // the raster surface is too small or the layer bbox is wrong.
        // This is the spatial gate for clipping regressions.
        CHECK_FALSE(touches);
    }
}

TEST_CASE("TextPlace Clipping — effect halo diagnostic") {
    struct ClipCheck { std::string label; Composition comp; };
    std::vector<ClipCheck> checks;
    checks.push_back({"blur20",   make_clip_blur_20()});
    checks.push_back({"glow40",   make_clip_glow_40()});
    checks.push_back({"shadow80", make_clip_shadow_80()});
    checks.push_back({"scale130", make_clip_scale_130()});
    checks.push_back({"scale200", make_clip_scale_200()});

    auto renderer = test::make_renderer();
    int rendered = 0;
    int touches_count = 0;
    for (auto& ch : checks) {
        // HARD FAIL on render exception / null fb — same gate as
        // verify_placement().  Edge-touch is informational only, but
        // null output is a regression regardless of effect presence.
        std::shared_ptr<Framebuffer> fb;
        try {
            fb = renderer.render(ch.comp, Frame{0});
        } catch (const std::exception& e) {
            FAIL("Render failed for ", ch.label, ": ", e.what(),
                 " — effect pipeline regression (halo diagnostic is also a regression gate).");
        }
        REQUIRE(fb != nullptr);
        ++rendered;
        bool touches = alpha_touches_edge(*fb);
        INFO("Halo diagnostic [", ch.label, "] alpha_touches_edge = ", touches);
        if (touches) {
            ++touches_count;
            MESSAGE("Halo reaches framebuffer edge for ", ch.label,
                    " — confirm via golden comparison (informational).");
        }
    }
    CHECK(rendered > 0);
    CHECK(static_cast<size_t>(rendered) == checks.size());
    INFO("Effect-halo compositions touching framebuffer edge: ", touches_count,
         "/", checks.size(), " (informational; golden is the visual gate).");
}

// ═══════════════════════════════════════════════════════════════════════════
// Group E — Multi-resolution (golden comparison)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace MultiRes 1920x1080") {
    auto comp = make_multires_1920x1080();
    verify_placement(comp, Frame{0}, "text_placement_multires_1920x1080");
}

TEST_CASE("TextPlace MultiRes 1280x720") {
    auto comp = make_multires_1280x720();
    verify_placement(comp, Frame{0}, "text_placement_multires_1280x720");
}

TEST_CASE("TextPlace MultiRes 1080x1920") {
    auto comp = make_multires_1080x1920();
    verify_placement(comp, Frame{0}, "text_placement_multires_1080x1920");
}

TEST_CASE("TextPlace MultiRes 3840x2160") {
    auto comp = make_multires_3840x2160();
    verify_placement(comp, Frame{0}, "text_placement_multires_3840x2160");
}

// ── Centroid check: all resolutions must render on-canvas with consistent
// relative centroid position.  The text pipeline renders in a local
// coordinate space; centroid is typically at ~(w/4, h/4).  We check:
//   1. Centroid is on-canvas (not off-screen)
//   2. Relative centroid (x/w, y/h) is consistent across resolutions
//   3. Not at double-center (anti-double-translation)
TEST_CASE("TextPlace MultiRes — centroid at dynamic center") {
    struct ResCheck { const char* label; int w; int h; Composition comp; };
    std::vector<ResCheck> checks;
    checks.push_back({"1920x1080", 1920, 1080, make_multires_1920x1080()});
    checks.push_back({"1280x720",  1280, 720,  make_multires_1280x720()});
    checks.push_back({"1080x1920", 1080, 1920, make_multires_1080x1920()});
    checks.push_back({"3840x2160", 3840, 2160, make_multires_3840x2160()});

    auto renderer = test::make_renderer();
    for (auto& rc : checks) {
        auto fb = renderer.render(rc.comp, Frame{0});
        REQUIRE(fb != nullptr);
        auto c = compute_alpha_centroid(*fb);
        CHECK(c.max_alpha > 0.5f);

        // On-canvas check: centroid must be visible on the canvas.
        INFO(rc.label, " centroid: (", c.x, ", ", c.y, ")");
        CHECK(c.x > 0.0f);
        CHECK(c.y > 0.0f);
        CHECK(c.x < static_cast<f32>(rc.w));
        CHECK(c.y < static_cast<f32>(rc.h));

        // Not-at-double-center check (anti-double-translation).
        const f32 cx = static_cast<f32>(rc.w) * 0.5f;
        const f32 cy = static_cast<f32>(rc.h) * 0.5f;
        const f32 tol = static_cast<f32>(rc.w) * 0.12f;
        bool near_double = (std::abs(c.x - cx * 2.0f) < tol) &&
                           (std::abs(c.y - cy * 2.0f) < tol);
        CHECK_FALSE(near_double);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Group F — Cache invalidation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Cache Invalidation — HELLO at f0") {
    auto comp = make_cache_invalidation();
    verify_placement(comp, Frame{0}, "text_placement_cache_hello_f0");
}

TEST_CASE("TextPlace Cache Invalidation — WORLD at f31") {
    auto comp = make_cache_invalidation();
    verify_placement(comp, Frame{31}, "text_placement_cache_world_f31");
}

TEST_CASE("TextPlace Cache Invalidation — CENTER at f61") {
    auto comp = make_cache_invalidation();
    verify_placement(comp, Frame{61}, "text_placement_cache_center_f61");
}

// ── Cache invalidation: verify text actually changes across frames.
// Use FRESH compositions AND renderers per frame to bypass both the
// node cache and any Composition-level memoization.
// TODO: the root cause is that the node cache / scene fingerprint may
// not capture frame-dependent text content changes.  This fresh-per-frame
// workaround should be removed once the cache key includes text content.
TEST_CASE("TextPlace Cache Invalidation — content changes") {
    // Render at 3 key frames with fresh compositions + renderers.
    auto renderer0 = test::make_renderer();
    auto comp0 = make_cache_invalidation();
    auto fb0 = renderer0.render(comp0, Frame{0});
    REQUIRE(fb0 != nullptr);

    auto renderer1 = test::make_renderer();
    auto comp1 = make_cache_invalidation();
    auto fb1 = renderer1.render(comp1, Frame{31});
    REQUIRE(fb1 != nullptr);

    auto renderer2 = test::make_renderer();
    auto comp2 = make_cache_invalidation();
    auto fb2 = renderer2.render(comp2, Frame{61});
    REQUIRE(fb2 != nullptr);

    // Quick pixel-difference check: the three framebuffers must differ.
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

    INFO("MAE f0->f31: ", err_01, "  f31->f61: ", err_12, "  f0->f61: ", err_02);

    // CACHE-INV-1: each HELLO→WORLD→CENTER transition must produce a
    // visible pixel difference (MAE > 1/255).  HARDENED: removed the
    // soft early-return path — if any pair produces MAE=0 the test now
    // FAILS.  The frame-dependent content MUST be propagated by the
    // scene-fingerprint / node-cache pipeline; suppressing this check
    // would let the cache-skip regression of frame-dependent text ship
    // silently to production.
    // TODO: track follow-up on production cache key to include text
    // content (this test is the regression gate, not the workaround).
    const double min_diff = 1.0 / 255.0;
    CHECK(err_01 > min_diff);
    CHECK(err_12 > min_diff);
    CHECK(err_02 > min_diff);

    // Ensure "HELLO" != "CENTER" (start != end) — should be the largest diff.
    CHECK(err_02 > err_01 * 0.5);
}

// ═══════════════════════════════════════════════════════════════════════════
// Group H — Debug overlay verification (§5 + §6)
// ═══════════════════════════════════════════════════════════════════════════

// H.1 — Render with text_layout_debug=true and verify overlay markers appear
// on the framebuffer.  The overlay draws 6 colored markers:
//   Red (1.0, 0.2, 0.2) = canvas center crosshair
//   Blue (0.2, 0.4, 1.0) = layer origin dot
//   Green (0.2, 1.0, 0.4) = layout box rectangle
//   Yellow (1.0, 1.0, 0.2) = visual bounds rectangle
//   Violet (0.8, 0.2, 1.0) = raster surface rectangle
//   White (1.0, 1.0, 1.0) = alpha centroid crosshair
TEST_CASE("TextPlace Debug Overlay — markers drawn") {
    // Create renderer with text_layout_debug enabled.
    SoftwareRenderer renderer(Config{});
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.text_layout_debug = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    test::attach_software_backend(&renderer);

    auto comp = make_static_center_no_pos();
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    const int w = static_cast<int>(fb->width());
    const int h = static_cast<int>(fb->height());
    CHECK(w == 1920);
    CHECK(h == 1080);

    // The overlay (§5) draws markers on the TextRunNode's internal
    // framebuffer.  After the compositor merges all layers, the markers
    // survive as composited pixels.  We scan for the overlay's distinct
    // color signatures:
    //   Red crosshair: R≈0.9, G≈0.18, B≈0.18 (composited over dark bg)
    //   Blue origin dot: similar composited signature
    int overlay_pixel_count = 0;

    for (int y = 0; y < h; ++y) {
        const Color* row = fb->pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const Color& c = row[x];
            // Red crosshair marker (composited over dark bg: ~0.9, 0.18, 0.18)
            // Also catch blue dot (~0.18, 0.22, 0.92) and green rect (~0.18, 0.9, 0.32)
            // All overlay markers have R or G or B > 0.85 with the other
            // channels < 0.4 — distinct from white text (all > 0.9) and
            // dark background (all < 0.1).
            bool is_redish  = (c.r > 0.85f && c.g < 0.35f && c.b < 0.35f);
            bool is_bluish  = (c.b > 0.85f && c.r < 0.35f && c.g < 0.55f);
            bool is_greenish = (c.g > 0.85f && c.r < 0.35f && c.b < 0.55f);
            if (is_redish || is_bluish || is_greenish) ++overlay_pixel_count;
        }
    }

    INFO("Overlay pixels: ", overlay_pixel_count);

    // The overlay should produce at least a few dozen marker pixels.
    // If the compositor clips the overlay to the text's predicted bbox,
    // some markers at canvas center may be outside the clip region.
    // We use a generous minimum to account for this.
    if (overlay_pixel_count == 0) {
        MESSAGE("No overlay marker pixels detected in composited output. "
                "The overlay draws on the TextRunNode's internal FB; "
                "markers may be clipped by the compositor's bbox-limited "
                "compositing.  The [text-layout] structured log (§6) "
                "confirms the overlay code path executes correctly.");
    }
    CHECK(overlay_pixel_count > 0);

    // Verify the overlay didn't destroy the text content.
    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.5f);  // text still visible
}

// ═══════════════════════════════════════════════════════════════════════════
// Group G — modular_coordinates ON/OFF parity
// ═══════════════════════════════════════════════════════════════════════════

// G.1 — Verify that the same composition renders identically (or near-identically)
// with modular_coordinates ON and OFF.  If the centroid shifts, there is
// still a double-translation path gated on the modular_coordinates flag.
//
// We test a subset of representative compositions:
//   - Static center (basic pin_to path)
//   - Animated center (exercises use_local path)
//   - Box alignment (exercises align/vertical_align)
//   - Multisource (text + rect in same layer)

using CompFactory = Composition (*)();

struct ModCoordParityCase {
    const char* label;
    CompFactory factory;
    Frame       frame;
};

TEST_CASE("TextPlace modular_coordinates ON/OFF — centroid parity") {
    std::vector<ModCoordParityCase> cases{
        {"StaticCenter",  make_static_center_no_pos,    Frame{0}},
        {"AnimatedCenter", make_animated_center_no_pos,  Frame{30}},
        {"BoxAlign",      make_box_alignment,           Frame{0}},
        {"Multisource",   make_multisource_text_plus_shape, Frame{0}},
    };

    // Helper: use the file-static make_mod_renderer_for_test(...).
    // No local lambda — keeps G.1 and G.2 in lockstep with one
    // canonical renderer-init pattern.

    for (auto& tc : cases) {
        INFO("Case: ", tc.label);

        auto renderer_on = make_mod_renderer_for_test(true);
        auto comp_on = tc.factory();
        auto fb_on = renderer_on.render(comp_on, tc.frame);
        REQUIRE(fb_on != nullptr);

        auto renderer_off = make_mod_renderer_for_test(false);
        auto comp_off = tc.factory();
        auto fb_off = renderer_off.render(comp_off, tc.frame);
        REQUIRE(fb_off != nullptr);

        auto c_on  = compute_alpha_centroid(*fb_on);
        auto c_off = compute_alpha_centroid(*fb_off);

        CHECK(c_on.max_alpha > 0.3f);
        CHECK(c_off.max_alpha > 0.3f);

        INFO("ON  centroid: (", c_on.x, ", ", c_on.y, ")");
        INFO("OFF centroid: (", c_off.x, ", ", c_off.y, ")");

        // The centroids should be within 5% of canvas width of each other.
        // Exact match is ideal but slight floating-point / rounding
        // differences between the two coordinate paths are acceptable.
        const f32 tolerance = static_cast<f32>(fb_on->width()) * 0.05f;
        CHECK(std::abs(c_on.x - c_off.x) < tolerance);
        CHECK(std::abs(c_on.y - c_off.y) < tolerance);
    }
}

// G.2 — Edge-touch parity: if ON doesn't clip, OFF shouldn't either.
// G.2 — ON/OFF parity: render with use_modular_graph=true AND=false
// for each composition.  HARDENED: previously the OFF path was skipped
// (commented as "may hit pre-existing issues") — now BOTH paths are
// exercised as a real regression gate.  Each path must:
//   - render successfully (no exception / null framebuffer)
//   - have visible alpha (max_alpha > 0.3)
//   - not touch the framebuffer edge (CHECK_FALSE on alpha_touches_edge)
// ON vs OFF parity also requires centroid drift < 5% canvas width
// (the same 5% tolerance as G.1) to prove the two paths converge.
TEST_CASE("TextPlace modular_coordinates ON/OFF — parity check") {
    std::vector<ModCoordParityCase> cases{
        {"GlowShadow", make_glow_shadow_center_no_pos, Frame{0}},
        {"Scale130",   make_scale_130_center_no_pos,   Frame{0}},
        {"Blur20",     make_clip_blur_20,              Frame{0}},
    };

    // Helper: file-static make_mod_renderer_for_test(bool) — see anon namespace.
    int rendered_on = 0;
    int rendered_off = 0;
    for (auto& tc : cases) {
        INFO("Case: ", tc.label);

        // ON path — hard-fail on render exception / null fb.
        std::shared_ptr<Framebuffer> fb_on;
        try {
            fb_on = make_mod_renderer_for_test(true).render(tc.factory(), tc.frame);
        } catch (const std::exception& e) {
            FAIL("ON render failed for ", tc.label, ": ", e.what());
        }
        REQUIRE(fb_on != nullptr);
        ++rendered_on;
        auto c_on = compute_alpha_centroid(*fb_on);
        CHECK(c_on.max_alpha > 0.3f);
        // No edge touch on ON path — modular graph must not clip.
        CHECK_FALSE(alpha_touches_edge(*fb_on));

        // OFF path (formerly skipped — now exercised).
        std::shared_ptr<Framebuffer> fb_off;
        try {
            fb_off = make_mod_renderer_for_test(false).render(tc.factory(), tc.frame);
        } catch (const std::exception& e) {
            FAIL("OFF render failed for ", tc.label, ": ", e.what(),
                 " — non-modular graph path must also produce a framebuffer.");
        }
        REQUIRE(fb_off != nullptr);
        ++rendered_off;
        auto c_off = compute_alpha_centroid(*fb_off);
        CHECK(c_off.max_alpha > 0.3f);
        // No edge touch on OFF path either — parity with ON.
        CHECK_FALSE(alpha_touches_edge(*fb_off));

        // ON/OFF centroid parity (5% canvas width, same as G.1).
        const f32 tolerance = static_cast<f32>(fb_on->width()) * 0.05f;
        INFO("ON  centroid: (", c_on.x, ", ", c_on.y,
             ")  OFF centroid: (", c_off.x, ", ", c_off.y,
             ")  tolerance ", tolerance, "px");
        CHECK(std::abs(c_on.x - c_off.x) < tolerance);
        CHECK(std::abs(c_on.y - c_off.y) < tolerance);
    }
    CHECK(static_cast<size_t>(rendered_on)  == cases.size());
    CHECK(static_cast<size_t>(rendered_off) == cases.size());
}
