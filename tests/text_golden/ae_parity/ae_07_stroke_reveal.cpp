// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_07_stroke_reveal.cpp
//
// TICKET-AE-PARITY-CINEMATIC-07 — Scene 07: cinematic stroke-then-fill
// reveal.  AE-equivalent of the "Outline → Fill" text-mode entrance: the
// glyphs first appear as outline-only at f00 (heavy stroke visible, fill
// alpha near-zero — text reads as silhouette stroke); through f15 the
// inner fill alpha interpolates 0.05 → 0.55 (text reads as half-outline /
// half-fill cream); by f30 the fill is fully composited (alpha=1.0) with
// the ink-stroke framing the silhouette edge in cinematic poster-typography
// fashion.
//
//   Channel │ f00              │ f15              │ f30              │
//   ─────────┼──────────────────┼──────────────────┼──────────────────│
//   stroke_w │ 8.0 (silhouette) │ 8.0 (silhouette) │ 6.0 (settled)    │
//   fill α   │ 0.05 (outline)   │ 0.55 (emerging)  │ 1.00 (composited)│
//   fill RGB │ amber-tinted     │ cream-yellow     │ pure white       │
//
// Stroke paint-order: per canonical C3D semantics, stroke is drawn AROUND
// fill (medial-edge ink), NOT OVER.  At f00 fill α=0.05 means the glyph
// silhouette is dominated by the stroke; at f30 fill α=1.0 means the
// visible glyph is fill-dominated with stroke contributing only to the
// silhouette edge.
//
// Cross-link ae_04_fill_stroke_shadow (already shipped) — same .paint block
// pattern (.paint.stroke_enabled / .stroke_color / .stroke_width); ae_07
// locks the stroke-then-fill choreography as a deterministic cinematic
// reveal (multi-channel: stroke paint scaffold stable + fill color α + RGB
// interpolate + stroke width settle).
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots f00/f15/f30.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `AppearanceSpec.paint.stroke_*` + `AppearanceSpec.color alpha-fade`
// + `verify_golden(*fb, ...)`; no legacy TextShape / rich_text references.
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

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

GoldenTestConfig make_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_07_stroke_reveal";
    cfg.mode               = golden_mode_from_environment();
    // Acceptance contract: per-frame material diff from prior frame (fill
    // alpha interpolates 0.05→1.00 → silhouette goes from stroke-only to
    // fully composited; stroke width settles 8→6 at f30).  Looser than the
    // static-paint ae_04 baseline but tighter than the multi-channel-keyframe
    // ae_06 baseline (stroke is stable in ae_07's middle frames, only fill
    // alpha + slight stroke settle are animated between snapshots).
    cfg.threshold.max_mean_abs_error       = 10.0f / 255.0f;
    cfg.threshold.max_abs_error            = 130.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.90f;
    cfg.threshold.max_rmse                 = 12.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.65f;
    return cfg;
}

// Stroke width per-frame: stable 8.0f for the in-flight reveal frames
// (silhouette dominates the visual); settles to 6.0f at f30 (typographic
// finalisation — fill now dominates the glyph body so the outline
// contributes only to the silhouette edge).
static float stroke_width_for(std::size_t f) {
    if (f == 0)  return 8.0f;
    if (f <= 15) return 8.0f;
    return 6.0f;
}

// Fill color per-frame: amber-tinted at f00 (matching stroke-cream
// anticipation), cream-yellow at f15, pure white at f30 (settled typography).
// The alpha channel is encoded via fill_color_for() so the stroke reads
// AROUND fill (paint-order canonical) and the silhouette remains stable
// across frames (stroke paint-block unchanged).
static Color fill_color_for(std::size_t f) {
    if (f == 0)  return Color{1.00f, 0.92f, 0.40f, 0.05f}; // near-invisible amber
    if (f <= 15) return Color{1.00f, 0.96f, 0.65f, 0.55f}; // emerging cream
    return         Color{1.00f, 1.00f, 0.98f, 1.00f};       // pure white settled
}

// 16:9 (1920x1080) cinematic stroke-reveal hero (title-card entrance).
Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/07/stroke_reveal/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float sw   = stroke_width_for(frame_idx);
                const Color fill = fill_color_for(frame_idx);
                l.text("stroke_reveal", {
                    .content = {.value = "OUTLINE -> FILL"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 220.0f},
                    .layout = {.box = {1700.0f, 360.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {
                        .color = fill,
                        .paint = {
                            .stroke_enabled = true,
                            .stroke_color    = Color{0.05f, 0.05f, 0.10f, 1.0f}, // near-black ink
                            .stroke_width    = sw
                        }
                    },
                    .position = {960.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

// 9:16 (1080x1920) portrait variant — same motion envelope, tighter box.
Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/07/stroke_reveal/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float sw   = stroke_width_for(frame_idx);
                const Color fill = fill_color_for(frame_idx);
                l.text("stroke_reveal", {
                    .content = {.value = "OUTLINE -> FILL"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 156.0f},
                    .layout = {.box = {1000.0f, 280.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {
                        .color = fill,
                        .paint = {
                            .stroke_enabled = true,
                            .stroke_color    = Color{0.05f, 0.05f, 0.10f, 1.0f},
                            .stroke_width    = sw
                        }
                    },
                    .position = {540.0f, 960.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

// ─── 16:9 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 07 stroke_reveal 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_07_stroke_reveal_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 07 stroke_reveal 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_07_stroke_reveal_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 07 stroke_reveal 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_07_stroke_reveal_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

// ─── 9:16 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 07 stroke_reveal 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_07_stroke_reveal_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 07 stroke_reveal 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_07_stroke_reveal_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 07 stroke_reveal 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_07_stroke_reveal_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}
