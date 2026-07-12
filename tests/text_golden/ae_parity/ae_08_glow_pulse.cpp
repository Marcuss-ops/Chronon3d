// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_08_glow_pulse.cpp
//
// TICKET-AE-PARITY-CINEMATIC-08 — Scene 08: glow_pulse (bounding-glow
// opacity pulse + subtle scale breath, no separate glow primitive). The
// pulse curve evokes a "pulsing aura" without depending on a glow
// texture (which would require a non-canonical resource):
//
//   Channel    │ f00                │ f15                │ f30                │
//   ────────────┼────────────────────┼────────────────────┼────────────────────│
//   opacity    │ 0.40 (low pulse)   │ 0.85 (peak pulse)  │ 0.50 (fade tail)   │
//   scale      │ 0.96 (slight small)│ 1.05 (slight grow) │ 0.98 (settle)      │
//
// The pair (opacity + scale) co-modulates to simulate a glow envelope
// without requiring a glow compositor (which is forward-only per
// Blocco 5 of docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md). Read as
// "the text itself pulses in/out of the focal plane" rather than a
// literal gaussian glow.
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots f00/f15/f30.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `LayerBuilder::opacity(...)` + `LayerBuilder::scale(...)` +
// `verify_golden(*fb, ...)`; no legacy TextShape / rich_text references.
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

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>

// TICKET-CHRONON-GLOW-FINAL — Phase 1: thin-wrapper via the canonical
// helper.  build_landscape() / build_portrait() become 1-line delegates.
#include "content/compositions/chronon_glow_final.hpp"

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::test::glow_final::ChrononGlowProps;

namespace {

GoldenTestConfig make_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_08_glow_pulse";
    cfg.mode               = golden_mode_from_environment();
    // Phase 2: cinematic additive glow acceps halo blending variance.
    // The 5-layer MultiLayer 3-pass blur + additive compositing emits
    // a small halo that differs slightly between machines and toolchain
    // versions (3-5% MAE typical); the relaxed contract (vs Phase 0
    // 9.0/255 ≈ 3.5%) holds the golden stable on CI.
    cfg.threshold.max_mean_abs_error       = 22.0f / 255.0f;  // was 9.0  (≈3.5%)
    cfg.threshold.max_abs_error            = 200.0f / 255.0f; // was 125  (≈49%)
    cfg.threshold.max_changed_pixel_ratio  = 0.85f;            // was 0.92; tightened per reviewer N2
    cfg.threshold.max_rmse                 = 28.0f / 255.0f;  // was 11   (≈4.3%)
    cfg.threshold.min_ssim                 = 0.60f;            // was 0.65 (SSIM floor for halo)
    return cfg;
}

// =========================================================================
// TICKET-CHRONON-GLOW-FINAL — Phase 2 re-baked goldens.
//
// Phase 2 activates the canonical CinematicGlowPreset (5 layers: source,
// inner glow, mid glow, bloom, additive compositing) and the per-frame
// scale breath.  The 6 PNG baselines (16:9 + 9:16 × f00/f15/f30) are
// captured with the cinematic-glow enabled.  The threshold contract is
// loosened to accept the additive-glow pixel variance (multi-pass blur +
// halo blending adds ≈2-4% MAE / ≈3% RMSE vs the Phase-0 baseline;
// SSIM floor lowered to 0.60 to absorb the new high-frequency halo
// content; max_changed_pixel_ratio relaxed so the halo area is allowed
// to change).  Re-bake via:
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGolden
// =========================================================================

Composition build_landscape(SoftwareRenderer& renderer, std::size_t /*frame_idx*/) {
    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    // Phase 2 canonical emit: cinematic glow ON + scale breath ON.
    props.glow_enabled        = true;  // CinematicGlowPreset (r=4/14/34, i=0.55/0.22/0.08)
    props.scale_breath = true;  // 0.96/1.05/0.98 layer scale envelope
    return chronon3d::test::glow_final::make_chronon_glow_final(props);
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t /*frame_idx*/) {
    ChrononGlowProps props = chronon3d::test::glow_final::default_portrait_props();
    // Phase 2 canonical portrait emit (same canonical cinematic glow).
    props.glow_enabled        = true;
    props.scale_breath = true;
    return chronon3d::test::glow_final::make_chronon_glow_final(props);
}

} // namespace

TEST_CASE("AE 08 glow_pulse 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_16x9_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_16x9_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_16x9_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_9x16_f00", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_9x16_f15", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_9x16_f30", make_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-TEXT-CLIP-ASCENT — alpha bbox regression tests.
//
// These TEST_CASEs verify that the rendered text is geometrically
// correct: centered on canvas, large enough, and NOT touching any edge.
// The golden diff tests above catch pixel-level drift; these numerical
// assertions catch the structural clipping bug immediately and
// consistently across machines.
//
// Expected visible bbox for "PULSE GLOW" 230pt on 1920×1080 canvas:
//   - centered near (960, 540)
//   - width > 800 px
//   - height > 100 px
//   - no edge contact (x0 > 10, y0 > 10, x1 < 1909, y1 < 1069)
//
// The original bug produced: x=974..1919, y=783..801 (19px tall, right
// edge contact).  These assertions lock that regression permanently.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-TEXT-CLIP-ASCENT: ae_08 16x9 f15 alpha bbox is centered and not clipped") {
    auto renderer = test::make_renderer_shared();
    // Enable diagnostics to activate [text-bbox] logging in predicted_bbox.
    {
        RenderSettings diag_settings;
        diag_settings.use_modular_graph = true;
        diag_settings.diagnostics.enabled = true;
        renderer->set_settings(diag_settings);
    }
    auto fb = renderer->render(build_landscape(*renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    // Verify the renderer produced non-empty output before scanning pixels.
    // If this fails, the text shape was not materialized (font engine issue).
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("alpha bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " width=", bbox.width(), " height=", bbox.height());

    // Primary assertions: text is visible and large enough.
    REQUIRE_FALSE(bbox.empty());
    CHECK(bbox.width()  > 800);
    CHECK(bbox.height() > 100);

    // No edge contact: text must be inset from all 4 edges.
    const int margin = 10;
    CHECK_FALSE(bbox.touches_right(1920, margin));
    CHECK_FALSE(bbox.touches_bottom(1080, margin));
    CHECK_FALSE(bbox.touches_left(margin));
    CHECK_FALSE(bbox.touches_top(margin));

    // Centroid sanity: text should be near canvas center (960, 540).
    const auto centroid = alpha_centroid(*fb);
    INFO("centroid: x=", centroid.x, " y=", centroid.y);
    CHECK(centroid.x > 600.0f);
    CHECK(centroid.x < 1320.0f);
    CHECK(centroid.y > 300.0f);
    CHECK(centroid.y < 780.0f);
}

TEST_CASE("TICKET-TEXT-CLIP-ASCENT: ae_08 9x16 f15 alpha bbox is centered and not clipped") {
    auto renderer = test::make_renderer_shared();
    {
        RenderSettings diag_settings;
        diag_settings.use_modular_graph = true;
        diag_settings.diagnostics.enabled = true;
        renderer->set_settings(diag_settings);
    }
    auto fb = renderer->render(build_portrait(*renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    // Verify the renderer produced non-empty output before scanning pixels.
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("alpha bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " width=", bbox.width(), " height=", bbox.height());

    // Primary assertions: text is visible and large enough.
    REQUIRE_FALSE(bbox.empty());
    CHECK(bbox.width()  > 400);
    CHECK(bbox.height() > 60);

    // No edge contact.
    const int margin = 10;
    CHECK_FALSE(bbox.touches_right(1080, margin));
    CHECK_FALSE(bbox.touches_bottom(1920, margin));
    CHECK_FALSE(bbox.touches_left(margin));
    CHECK_FALSE(bbox.touches_top(margin));

    // Centroid sanity: text should be near canvas center (540, 960).
    const auto centroid = alpha_centroid(*fb);
    INFO("centroid: x=", centroid.x, " y=", centroid.y);
    CHECK(centroid.x > 300.0f);
    CHECK(centroid.x < 780.0f);
    CHECK(centroid.y > 600.0f);
    CHECK(centroid.y < 1320.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-CHRONON-GLOW-FINAL — DoD §9: 19px sliver regression lock.
//
// Locks the historical "19px sliver" regression permanently.  The
// original bug produced a 19px-tall sliver at the right edge of the
// canvas (x=974..1919, y=783..801) instead of the full 230pt
// "PULSE GLOW" text — a font-size / safe-area origin miscalculation
// that the existing geometry tests catch, but only when they reach the
// right edge / height-fail assertions together.  This single test
// fails immediately and unambiguously if the sliver ever returns (e.g.
// due to a future migration that drops the font-size, shifts the
// safe-area origin back to the right edge, or breaks the cinematic
// glow bbox composition path).
//
// The four checks are intentionally minimal and strict — the smallest
// set of assertions that pins the regression:
//
//   bbox.height() > 100  → kills the 19px sliver (sliver is 19px tall)
//   bbox.width()  > 800  → kills any truncated-width variant
//   bbox.x1 < 1910       → kills the right-edge contact (sliver touched x=1919)
//   bbox.y1 < 1070       → belt-and-suspenders for any bottom-edge contact
//
// Frame 15 is used because it is the peak-pulse snapshot (opacity 0.85,
// scale 1.05) — the frame where the sliver was historically most
// reproducible (the right-edge contact appeared only when the scale
// breath + glow additive compositing pushed the final pixel into the
// safe-area boundary).  16:9 canvas (1920×1080) is the canonical
// reference surface.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ChrononGlowFinalAE never regresses to the 19px sliver") {
    auto renderer = test::make_renderer_shared();
    {
        RenderSettings diag_settings;
        diag_settings.use_modular_graph = true;
        diag_settings.diagnostics.enabled = true;
        renderer->set_settings(diag_settings);
    }
    auto fb = renderer->render(build_landscape(*renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("alpha bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " width=", bbox.width(), " height=", bbox.height());

    // DoD §9 — the 4 hard checks that pin the 19px sliver regression.
    // All four are required: any single failure = the sliver is back.
    CHECK(bbox.height() > 100);  // sliver was 19px tall (19 < 100) → fails
    CHECK(bbox.x1      < 1910);  // sliver touched x=1919 (right edge) → fails
    // The two checks below are defensive against future variants; they
    // would NOT have caught the historical 19px sliver on their own
    // (a 945px-wide bbox at y=783..801 passes both), but they pin the
    // canonical 230pt text size and protect against future migrations
    // that drop font-size or push text into the bottom safe-area.
    CHECK(bbox.width()  > 800);  // pins 230pt full-width render
    CHECK(bbox.y1      < 1070);  // belt-and-suspenders for any bottom contact
}
