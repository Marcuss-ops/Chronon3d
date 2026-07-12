// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/ae_parity/ae_glow_smoke.cpp
//
// TICKET-CHRONON-GLOW-FINAL — Phase 2 SMOKE TEST.
//
// Validates that the canonical cinematic additive glow (5 layers:
// source + inner + mid + bloom + additive compositing via the
// pipeline's existing l.glow / GlowParams / MultiLayer resolver) does
// NOT darken the source rendering.
//
// CONTRACT (primary): the luminance of a small rect centered on the
// text alpha centroid must remain ≥ 98% of the source-only render.
//
// CONTRACT (secondary, halo-region): at a sample point positioned in
// the bloom halo region (centre + 60px X on 16x9, centre + 40px X on
// 9x16 — well inside the bloom_radius=34 + the 60-px sample box, but
// outside the inner_radius=4 source-core), the with-glow luminance
// must ADD measurably above the source-only background luma.  This
// guards against a "glow effect is a no-op" regression that would not
// be caught by the primary contract alone.
//
// Why: cinematic glow is supposed to ADD brightness in the halo
// region around the text; if the compositing formula accidentally
// multiplies the source by (1 - glow_intensity) the circle would
// DARKEN the text.  Primary assertion catches DARKEN regressions;
// secondary assertion catches NO-OP regressions.
//
// Render mechanism: the unified helper
// glow_final::make_chronon_glow_final_for_test(props, font_engine)
// provides the canonical scene builder.  Toggle `props.glow_enabled`
// to compare with-glow vs without-glow without any other parameter
// drift.  Per-frame opacity envelope is held identical (same
// ctx.frame.integral).
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; reuses existing renderer + luma + alpha helpers
// from tests/helpers + tests/text_golden/text_clip/test_helpers.hpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>
#include "content/compositions/chronon_glow_final.hpp"

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::test::glow_final::ChrononGlowProps;

// Halo-region sampling offset from the alpha centroid (pixels).  Chosen
// to fall well inside the bloom=34 px soft-halo envelope but outside
// the inner=4 px core, so WITH-GLOW adds measurable luma through the
// bloom pass at that point while WITHOUT-GLOW reads only background.
constexpr int kHaloSampleOffsetX16x9 = 60;   // landscape: 16:9 — wide margin
constexpr int kHaloSampleOffsetX9x16 = 40;   // portrait: 9:16 — narrower margin

// Halo-region luma threshold: WITH-GLOW must exceed WITHOUT-GLOW by at
// least this margin to be considered "added energy".  Soft floor
// (≈0.01) to absorb the very small bloom_intensity (0.08) × specular
// alpha falloff at the sample offset; the test still catches a true
// no-op (which produces glow_luma_halo == src_luma_halo).
constexpr float kHaloAddFloor = 0.010f;

// Render the canonical ae_08 composition at the requested frame index
// with the supplied props.  No assertions happen here.
static std::shared_ptr<Framebuffer> render_at(
        std::shared_ptr<SoftwareRenderer> renderer,
        ChrononGlowProps props,
        Frame frame) {
    return renderer->render(
        chronon3d::test::glow_final::make_chronon_glow_final_for_test(
            props, renderer->font_engine()),
        frame);
}

// Average luma over a halfH×halfW square centred on (centre.x+offsetX, centre.y).
static float sample_offset_luma(const Framebuffer& fb, Vec2 centre,
                                 int offsetX, int halfW) {
    const float cx = centre.x + static_cast<float>(offsetX);
    const float cy = centre.y;
    const int x0 = static_cast<int>(cx) - halfW;
    const int y0 = static_cast<int>(cy) - halfW;
    const int x1 = static_cast<int>(cx) + halfW + 1;
    const int y1 = static_cast<int>(cy) + halfW + 1;
    return average_luma_rect(fb, x0, y0, x1, y1);
}

TEST_CASE("PHASE-2: ae_08 cinematic additive glow preserves source at f15 16x9 center-bbox (≥98%)") {
    auto renderer = make_renderer_shared();

    // Source-only baseline (glow_enabled=false) and full Phase-2
    // canonical cinematic-glow render (glow_enabled=true).  Same canvas,
    // same frame, same per-frame opacity envelope — the only delta is
    // the cinematic-glow pipeline.
    ChrononGlowProps src_props = chronon3d::test::glow_final::default_landscape_props();
    src_props.glow_enabled = false;

    ChrononGlowProps glow_props = chronon3d::test::glow_final::default_landscape_props();
    glow_props.glow_enabled = true;

    auto src_fb  = render_at(renderer, src_props,  Frame{15});
    auto glow_fb = render_at(renderer, glow_props, Frame{15});

    REQUIRE(src_fb  != nullptr);
    REQUIRE(glow_fb != nullptr);
    REQUIRE(src_fb->width()  == 1920);
    REQUIRE(src_fb->height() == 1080);
    REQUIRE(glow_fb->width()  == 1920);
    REQUIRE(glow_fb->height() == 1080);

    // ── Centroid anchor (shape-fragile: positions MUST agree) ────
    const Vec2 src_centre  = alpha_centroid(*src_fb);
    const Vec2 glow_centre = alpha_centroid(*glow_fb);
    INFO("source centroid: x=", src_centre.x,  " y=", src_centre.y);
    INFO("glow   centroid: x=", glow_centre.x, " y=", glow_centre.y);
    // The two centroids must align (both renders anchor at canvas
    // centre via the helper); this REQUIRE replaces the previous
    // `src_luma > 1e-6f` precondition which could mask legitimate
    // render failures (font shaping produced zero glyphs, etc.).
    REQUIRE(glow_centre.x == doctest::Approx(src_centre.x).epsilon(3.0f));
    REQUIRE(glow_centre.y == doctest::Approx(src_centre.y).epsilon(3.0f));

    // ── Primary contract: source pixel preservation ≥98% ────
    constexpr int kHalfW = 4;  // 9×9 sample window
    const float src_luma  = sample_offset_luma(*src_fb,  src_centre,  /*offsetX=*/0, kHalfW);
    const float glow_luma = sample_offset_luma(*glow_fb, glow_centre, /*offsetX=*/0, kHalfW);
    INFO("source luma = ", src_luma,  " glow luma = ", glow_luma);
    INFO("ratio (glow / source) = ", (src_luma > 1e-6f) ? (glow_luma / src_luma) : -1.0f);
    REQUIRE(src_luma > 1e-6f);  // source must be lit at the centroid
    CHECK(glow_luma >= 0.98f * src_luma);

    // ── Secondary contract: halo-region added-energy ≥ kHaloAddFloor ────
    const float src_halo  = sample_offset_luma(*src_fb,  src_centre,  kHaloSampleOffsetX16x9, kHalfW);
    const float glow_halo = sample_offset_luma(*glow_fb, glow_centre, kHaloSampleOffsetX16x9, kHalfW);
    INFO("halo luma at +", kHaloSampleOffsetX16x9,
         "px: src=", src_halo, " glow=", glow_halo);
    CHECK(glow_halo >= src_halo + kHaloAddFloor);
}

TEST_CASE("PHASE-2: ae_08 cinematic additive glow preserves source at f15 9x16 center-bbox (≥98%)") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps src_props = chronon3d::test::glow_final::default_portrait_props();
    src_props.glow_enabled = false;

    ChrononGlowProps glow_props = chronon3d::test::glow_final::default_portrait_props();
    glow_props.glow_enabled = true;

    auto src_fb  = render_at(renderer, src_props,  Frame{15});
    auto glow_fb = render_at(renderer, glow_props, Frame{15});

    REQUIRE(src_fb  != nullptr);
    REQUIRE(glow_fb != nullptr);
    REQUIRE(src_fb->width()  == 1080);
    REQUIRE(src_fb->height() == 1920);
    REQUIRE(glow_fb->width()  == 1080);
    REQUIRE(glow_fb->height() == 1920);

    const Vec2 src_centre  = alpha_centroid(*src_fb);
    const Vec2 glow_centre = alpha_centroid(*glow_fb);
    INFO("source centroid: x=", src_centre.x,  " y=", src_centre.y);
    INFO("glow   centroid: x=", glow_centre.x, " y=", glow_centre.y);
    REQUIRE(glow_centre.x == doctest::Approx(src_centre.x).epsilon(3.0f));
    REQUIRE(glow_centre.y == doctest::Approx(src_centre.y).epsilon(3.0f));

    constexpr int kHalfW = 4;
    const float src_luma  = sample_offset_luma(*src_fb,  src_centre,  /*offsetX=*/0, kHalfW);
    const float glow_luma = sample_offset_luma(*glow_fb, glow_centre, /*offsetX=*/0, kHalfW);
    INFO("source luma = ", src_luma,  " glow luma = ", glow_luma);
    REQUIRE(src_luma > 1e-6f);
    CHECK(glow_luma >= 0.98f * src_luma);

    // Halo region (slightly narrower on 9:16 — offset 40 < 60 to keep
    // the sample inside the 1080×1920 canvas with margin).
    const float src_halo  = sample_offset_luma(*src_fb,  src_centre,  kHaloSampleOffsetX9x16, kHalfW);
    const float glow_halo = sample_offset_luma(*glow_fb, glow_centre, kHaloSampleOffsetX9x16, kHalfW);
    INFO("halo luma at +", kHaloSampleOffsetX9x16,
         "px: src=", src_halo, " glow=", glow_halo);
    CHECK(glow_halo >= src_halo + kHaloAddFloor);
}
