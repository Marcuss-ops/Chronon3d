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
#include <tests/text_golden/text_clip/test_helpers.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

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

// Per-frame opacity for the pulse envelope.
static float opacity_for(std::size_t f) {
    if (f == 0)  return 0.40f;
    if (f <= 15) return 0.85f;
    return 0.50f;
}

// Per-frame uniform scale for the subtle breath.
static Vec3 scale_for(std::size_t f) {
    if (f == 0)  return Vec3{0.96f, 0.96f, 1.0f};
    if (f <= 15) return Vec3{1.05f, 1.05f, 1.0f};
    return         Vec3{0.98f, 0.98f, 1.0f};
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/08/glow_pulse/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx, &renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("glow_pulse", TextRunSpec{
                    .text = {
                        .content = {.value = "PULSE GLOW"},
                        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 230.0f},
                        .layout = {.box = {1700.0f, 360.0f},
                                   .align = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle},
                        .appearance = {.color = Color::white()},
                        .position = {960.0f, 540.0f, 0.0f}
                    }
                }).commit();
                l.opacity(opacity_for(frame_idx));
                l.scale(scale_for(frame_idx));
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/08/glow_pulse/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx, &renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("glow_pulse", TextRunSpec{
                    .text = {
                        .content = {.value = "PULSE GLOW"},
                        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 160.0f},
                        .layout = {.box = {1000.0f, 280.0f},
                                   .align = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle},
                        .appearance = {.color = Color::white()},
                        .position = {540.0f, 960.0f, 0.0f}
                    }
                }).commit();
                l.opacity(opacity_for(frame_idx));
                l.scale(scale_for(frame_idx));
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 08 glow_pulse 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 08 glow_pulse 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_08_glow_pulse_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
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
