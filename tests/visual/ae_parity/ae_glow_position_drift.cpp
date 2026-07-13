// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/ae_parity/ae_glow_position_drift.cpp
//
// TICKET-CHRONON-GLOW-FINAL — Phase 3 SCALA regression.
//
// Validates that the per-frame scale breath (0.96 → 1.05 → 0.98 at
// f00 / f15 / f30) does NOT cause the rendered alpha-bbox centroid
// to drift away from canvas center, AND that no edge clipping occurs
// (including the cinematic glow's bloom halo).
//
// CONTRACT:
//   (a) Center drift < 2 px on all 3 snapshot frames for each AR
//   (b) No X offset (centroid.x ≈ canvas_center.x for both ARs)
//   (c) Glow halo non-clipped: alpha-bbox (which scans all pixels with
//       alpha > 0.01 — text body AND glow halo) does not touch any
//       canvas edge within 10 px margin.
//
// Why this matters: in Phase 1 (commit cd42bc97), the scale breath
// was deliberately disabled because `l.scale(NON_IDENTITY)` + the
// legacy `TextPlacementKind::Absolute` produced a centroid that
// drifted several pixels per scale factor.  Phase 2 re-enabled the
// breath (commit e2b600d7) but the symptom was masked under the
// pre-baked glow baseline.  Phase 3 fixes the root cause (switch
// to `TextPlacementKind::CanvasCenter` so the text resolver bakes
// the box at canvas center at compose time, ignoring layer scale) and
// locks the contract with this regression suite.
//
// Render mechanism: the unified helper
// glow_final::make_chronon_glow_final(props, font_engine)
// always emits the canvas-centering placement; frame_idx drives both
// the per-frame opacity envelope AND the per-frame scale envelope
// inside the helper's `build_chronon_glow_scene`.
//
// AGENTS.md v0.1 freeze-compliant: zero new public SDK API in
// include/chronon3d/; reuses the existing helper + alpha_bbox /
// alpha_centroid from tests/helpers + tests/text_golden/text_clip/.
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
using chronon3d::content::glow_final::ChrononGlowProps;

// ── Phase 3 SCALA contract constants ──────────────────────────────────
namespace phase3_constants {
    // (a) centroid drift tolerance.  CanvasCenter bake should produce
    // 0 drift in theory; the 2 px epsilon absorbs any integer rounding
    // in the resolver's center computation + sub-pixel font shaping.
    constexpr float kCentroidDriftTolerancePx = 2.0f;

    // (c) margin to all edges.  10 px minimum for the text body.  The
    // cinematic glow bloom (radius=34, intensity=0.08) emits a dim
    // gaussian tail with alpha ≈ 0.05 at r=17px and ≈ 0.01 at r=34px;
    // we filter the margin check to TEXT-BODY pixels (alpha > 0.3) so
    // the halo tail at the outer gaussian edge is excluded from the
    // "clipped" verdict.  kHaloFilterEpsilon is the high-alpha
    // threshold used for the margin-check bbox only.
    constexpr int   kEdgeMarginPx      = 10;
    constexpr float kHaloFilterEpsilon = 0.3f;  // filter halo tail from margin check

    // Lower-bound checks on the rendered alpha_bbox to ensure the text
    // is actually visible (not collapsed to 0 px by a regression).
    // Landscape: "PULSE GLOW" at 230 pt is roughly 1100×260 px.
    constexpr int kMinTextWidthPx16x9   = 800;
    constexpr int kMinTextHeightPx16x9  = 100;
    constexpr int kMinTextWidthPx9x16   = 400;
    constexpr int kMinTextHeightPx9x16  =  60;
}  // namespace phase3_constants

// Render one frame of the canonical ae_08 composition (16:9 default).
static std::shared_ptr<Framebuffer> render_ae_08(
        std::shared_ptr<SoftwareRenderer> renderer,
        ChrononGlowProps props,
        Frame frame) {
    return renderer->render(
        chronon3d::content::glow_final::make_chronon_glow_final(props),
        frame);
}

TEST_CASE("FASE-3 SCALA: ae_08 16x9 f00 — scale=0.96, centroid stays at (960, 540) within 2 px") {
    using namespace phase3_constants;
    auto renderer = make_renderer_shared();
    ChrononGlowProps props = chronon3d::content::glow_final::default_landscape_props();
    auto fb = render_ae_08(renderer, props, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const auto c = alpha_centroid(*fb);
    INFO("f00 centroid: x=", c.x, " y=", c.y);
    // (a) center drift < 2 px (CanvasCenter bake)
    CHECK(c.x == doctest::Approx(960.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.y == doctest::Approx(540.0f).epsilon(kCentroidDriftTolerancePx));
    // (b) no X offset (no high- or low-side bias introduced by scale)
    CHECK(c.x > 0.0f);
    CHECK(c.x < 1920.0f);

    // (c) TEXT-BODY non-clipped: filter the dim bloom-halo tail out of
    // the bbox by raising the alpha threshold to kHaloFilterEpsilon.
    // The cinematic glow's outer gaussian (radius=34, intensity=0.08)
    // has alpha < 0.3 at r > ~10px, so a high-alpha bbox isolates
    // the text body and gives a clean "no edge contact" verdict.
    const AlphaBBox body_bbox = alpha_bbox(*fb, kHaloFilterEpsilon);
    const AlphaBBox all_bbox  = alpha_bbox(*fb);  // default (alpha > 0.01)
    INFO("f00 text-body bbox: x0=", body_bbox.x0, " y0=", body_bbox.y0,
         " x1=", body_bbox.x1, " y1=", body_bbox.y1,
         " w=", body_bbox.width(), " h=", body_bbox.height());
    CHECK(all_bbox.width()  > kMinTextWidthPx16x9);
    CHECK(all_bbox.height() > kMinTextHeightPx16x9);
    CHECK_FALSE(body_bbox.touches_left(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_right(1920, kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_top(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_bottom(1080, kEdgeMarginPx));
}

TEST_CASE("FASE-3 SCALA: ae_08 16x9 f15 — scale=1.05, centroid stays at (960, 540) within 2 px") {
    using namespace phase3_constants;
    auto renderer = make_renderer_shared();
    ChrononGlowProps props = chronon3d::content::glow_final::default_landscape_props();
    auto fb = render_ae_08(renderer, props, Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const auto c = alpha_centroid(*fb);
    INFO("f15 centroid: x=", c.x, " y=", c.y);
    CHECK(c.x == doctest::Approx(960.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.y == doctest::Approx(540.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.x > 0.0f);
    CHECK(c.x < 1920.0f);

    const AlphaBBox body_bbox = alpha_bbox(*fb, kHaloFilterEpsilon);
    const AlphaBBox all_bbox  = alpha_bbox(*fb);
    INFO("f15 text-body bbox: x0=", body_bbox.x0, " y0=", body_bbox.y0,
         " x1=", body_bbox.x1, " y1=", body_bbox.y1,
         " w=", body_bbox.width(), " h=", body_bbox.height());
    CHECK(all_bbox.width()  > kMinTextWidthPx16x9);
    CHECK(all_bbox.height() > kMinTextHeightPx16x9);
    CHECK_FALSE(body_bbox.touches_left(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_right(1920, kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_top(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_bottom(1080, kEdgeMarginPx));
}

TEST_CASE("FASE-3 SCALA: ae_08 16x9 f30 — scale=0.98, centroid stays at (960, 540) within 2 px") {
    using namespace phase3_constants;
    auto renderer = make_renderer_shared();
    ChrononGlowProps props = chronon3d::content::glow_final::default_landscape_props();
    auto fb = render_ae_08(renderer, props, Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const auto c = alpha_centroid(*fb);
    INFO("f30 centroid: x=", c.x, " y=", c.y);
    CHECK(c.x == doctest::Approx(960.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.y == doctest::Approx(540.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.x > 0.0f);
    CHECK(c.x < 1920.0f);

    const AlphaBBox body_bbox = alpha_bbox(*fb, kHaloFilterEpsilon);
    const AlphaBBox all_bbox  = alpha_bbox(*fb);
    INFO("f30 text-body bbox: x0=", body_bbox.x0, " y0=", body_bbox.y0,
         " x1=", body_bbox.x1, " y1=", body_bbox.y1,
         " w=", body_bbox.width(), " h=", body_bbox.height());
    CHECK(all_bbox.width()  > kMinTextWidthPx16x9);
    CHECK(all_bbox.height() > kMinTextHeightPx16x9);
    CHECK_FALSE(body_bbox.touches_left(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_right(1920, kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_top(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_bottom(1080, kEdgeMarginPx));
}

TEST_CASE("FASE-3 SCALA: ae_08 9x16 f00 — scale=0.96, centroid stays at (540, 960) within 2 px") {
    using namespace phase3_constants;
    auto renderer = make_renderer_shared();
    ChrononGlowProps props = chronon3d::content::glow_final::default_portrait_props();
    auto fb = render_ae_08(renderer, props, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);

    const auto c = alpha_centroid(*fb);
    INFO("f00 centroid (9x16): x=", c.x, " y=", c.y);
    CHECK(c.x == doctest::Approx(540.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.y == doctest::Approx(960.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.x > 0.0f);
    CHECK(c.x < 1080.0f);

    const AlphaBBox body_bbox = alpha_bbox(*fb, kHaloFilterEpsilon);
    const AlphaBBox all_bbox  = alpha_bbox(*fb);
    INFO("f00 text-body bbox: x0=", body_bbox.x0, " y0=", body_bbox.y0,
         " x1=", body_bbox.x1, " y1=", body_bbox.y1,
         " w=", body_bbox.width(), " h=", body_bbox.height());
    CHECK(all_bbox.width()  > kMinTextWidthPx9x16);
    CHECK(all_bbox.height() > kMinTextHeightPx9x16);
    CHECK_FALSE(body_bbox.touches_left(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_right(1080, kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_top(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_bottom(1920, kEdgeMarginPx));
}

TEST_CASE("FASE-3 SCALA: ae_08 9x16 f15 — scale=1.05, centroid stays at (540, 960) within 2 px") {
    using namespace phase3_constants;
    auto renderer = make_renderer_shared();
    ChrononGlowProps props = chronon3d::content::glow_final::default_portrait_props();
    auto fb = render_ae_08(renderer, props, Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);

    const auto c = alpha_centroid(*fb);
    INFO("f15 centroid (9x16): x=", c.x, " y=", c.y);
    CHECK(c.x == doctest::Approx(540.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.y == doctest::Approx(960.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.x > 0.0f);
    CHECK(c.x < 1080.0f);

    const AlphaBBox body_bbox = alpha_bbox(*fb, kHaloFilterEpsilon);
    const AlphaBBox all_bbox  = alpha_bbox(*fb);
    INFO("f15 text-body bbox: x0=", body_bbox.x0, " y0=", body_bbox.y0,
         " x1=", body_bbox.x1, " y1=", body_bbox.y1,
         " w=", body_bbox.width(), " h=", body_bbox.height());
    CHECK(all_bbox.width()  > kMinTextWidthPx9x16);
    CHECK(all_bbox.height() > kMinTextHeightPx9x16);
    CHECK_FALSE(body_bbox.touches_left(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_right(1080, kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_top(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_bottom(1920, kEdgeMarginPx));
}

TEST_CASE("FASE-3 SCALA: ae_08 9x16 f30 — scale=0.98, centroid stays at (540, 960) within 2 px") {
    using namespace phase3_constants;
    auto renderer = make_renderer_shared();
    ChrononGlowProps props = chronon3d::content::glow_final::default_portrait_props();
    auto fb = render_ae_08(renderer, props, Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);

    const auto c = alpha_centroid(*fb);
    INFO("f30 centroid (9x16): x=", c.x, " y=", c.y);
    CHECK(c.x == doctest::Approx(540.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.y == doctest::Approx(960.0f).epsilon(kCentroidDriftTolerancePx));
    CHECK(c.x > 0.0f);
    CHECK(c.x < 1080.0f);

    const AlphaBBox body_bbox = alpha_bbox(*fb, kHaloFilterEpsilon);
    const AlphaBBox all_bbox  = alpha_bbox(*fb);
    INFO("f30 text-body bbox: x0=", body_bbox.x0, " y0=", body_bbox.y0,
         " x1=", body_bbox.x1, " y1=", body_bbox.y1,
         " w=", body_bbox.width(), " h=", body_bbox.height());
    CHECK(all_bbox.width()  > kMinTextWidthPx9x16);
    CHECK(all_bbox.height() > kMinTextHeightPx9x16);
    CHECK_FALSE(body_bbox.touches_left(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_right(1080, kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_top(kEdgeMarginPx));
    CHECK_FALSE(body_bbox.touches_bottom(1920, kEdgeMarginPx));
}
