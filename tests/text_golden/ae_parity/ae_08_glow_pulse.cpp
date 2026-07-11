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
#include "tests/visual/ae_parity/glow_final_compositions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::test::glow_final::ChrononGlowProps;

namespace {

GoldenTestConfig make_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_08_glow_pulse";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 9.0f / 255.0f;
    cfg.threshold.max_abs_error            = 125.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.92f;
    cfg.threshold.max_rmse                 = 11.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.65f;
    return cfg;
}

// Per-frame envelope constants now live in the unified helper
// (glow_final::opacity_for_frame / glow_final::scale_for_frame).  The
// local opacity_for() and scale_for() helpers were removed to avoid
// drift between the golden test and the CLI factory.

Composition build_landscape(SoftwareRenderer& renderer, std::size_t /*frame_idx*/) {
    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    // Back-compat: the existing ae_08 golden baseline was captured
    // WITHOUT cinematic glow but WITH the scale breath (the original
    // build_landscape path called l.scale({0.96|1.05|0.98,…,1.0})).
    // Phase 2 will rebake against the glow-enabled render once the
    // Pixel-Match contract is updated; for now both flags match the
    // Phase-0 PNG baseline.
    props.glow_enabled        = false;  // no cinematic glow at Phase-0
    props.enable_scale_breath = true;   // scale breath was present in PNG
    return chronon3d::test::glow_final::make_chronon_glow_final_for_test(
        props, renderer.font_engine());
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t /*frame_idx*/) {
    ChrononGlowProps props = chronon3d::test::glow_final::default_portrait_props();
    // Same back-compat rationale as build_landscape (no cinematic glow,
    // WITH scale breath — matches Phase-0 PNG baseline).
    props.glow_enabled        = false;
    props.enable_scale_breath = true;
    return chronon3d::test::glow_final::make_chronon_glow_final_for_test(
        props, renderer.font_engine());
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
