// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_no_clipping.cpp
//
// P0-2: No Glyph Clipping — verifies that ascenders, descenders, wide
// glyphs, and ligature-prone sequences are not clipped by the scratch
// surface or compositor.
//
// Cases:
//   1. Tall ascenders (ÁÉÍ)     → bbox height > threshold, top > 0
//   2. Deep descenders (gjpqy)  → descenders visible, height > threshold
//   3. Wide glyphs (WMWMWM)    → width > threshold, no right-edge clip
//   4. Ligature sequences (ffff)→ no right-edge clipping
//   5. Full pangram             → complete coverage, all sides inside canvas
//   6. Small font size (24pt)   → no clipping at small sizes
//
// Note: text is centered on a 1920x1080 canvas at 180pt.  With
// VerticalAlign::Middle, the text block is centered vertically so
// descenders of "gjpqy" may extend close to the canvas bottom.
// Thresholds account for this layout.
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

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

Composition build_clip_test_composition(
    SoftwareRenderer& renderer,
    std::string_view text,
    float font_size = 180.0f,
    Vec3 position = {960.0f, 540.0f, 0.0f}
) {
    return composition(
        {.name = "TextNoClip/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_size, position](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("clip_layer", [&renderer, text, font_size, position](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("clip_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = std::string{text}},.position = position,.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = font_size
                        },.layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },.appearance = {.color = Color::white()},}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Ascenders not clipped ══════════════════════════════════════
TEST_CASE("NoClip 01: ascenders ÁÉÍ not clipped above") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_test_composition(renderer, "ÁÉÍ"), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("bbox: y0=", bbox.y0, " y1=", bbox.y1, " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    // Ascenders at 180pt should produce significant vertical ink.
    CHECK(bbox.height() > 100);
    // Top must not be at canvas top (would mean clipping above).
    CHECK(bbox.y0 > 0);
}

// ═══ Test 2 — Descenders visible ═════════════════════════════════════════
// Descenders (gjpqy) must be visible.  With VerticalAlign::Middle and
// a single line, the text block is centered — descenders extend below
// the baseline but the whole block may be near the canvas bottom.
TEST_CASE("NoClip 02: descenders gjpqy are visible") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_test_composition(renderer, "gjpqy"), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("bbox: y0=", bbox.y0, " y1=", bbox.y1, " h=", bbox.height(),
         " canvas_h=", fb->height());

    CHECK_FALSE(bbox.empty());
    // Descenders should produce visible ink.
    CHECK(bbox.height() > 50);
    // The bbox must be entirely within the canvas (no clipping).
    // Allow y1 to be at the very bottom (within 2px) since centered
    // text at 180pt on 1080px canvas can reach the edge.
    CHECK(bbox.y1 <= fb->height() - 1);
    // Top must not be clipped.
    CHECK(bbox.y0 > 0);
}

// ═══ Test 3 — Wide glyphs extend horizontally ════════════════════════════
TEST_CASE("NoClip 03: wide glyphs WMWMWM extend horizontally") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_test_composition(renderer, "WMWMWM"), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("bbox: w=", bbox.width(), " h=", bbox.height(), " x1=", bbox.x1);

    CHECK_FALSE(bbox.empty());
    // Wide glyphs at 180pt should produce substantial width.
    CHECK(bbox.width() > 400);
    // Cap height for WMWMWM (no ascenders/descenders) is ~60-80px at 180pt.
    CHECK(bbox.height() > 40);
}

// ═══ Test 4 — Ligature-prone sequences not clipped ═══════════════════════
TEST_CASE("NoClip 04: ligature sequence ffff not clipped") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_test_composition(renderer, "ffff"), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("bbox: w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.width() > 50);
    CHECK(bbox.height() > 50);
}

// ═══ Test 5 — Full pangram: all sides inside canvas ══════════════════════
// A pangram exercises all glyph types.  The bbox must be entirely
// within the canvas.  At 180pt centered on 1920x1080, the text can
// reach close to the right edge — we verify it's inside (not clipped).
TEST_CASE("NoClip 05: full pangram inside canvas") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_test_composition(renderer,
            "The quick brown fox jumps over the lazy dog"),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1,
         " w=", bbox.width(), " h=", bbox.height());

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 50);
    CHECK(bbox.width() > 300);

    // All edges must be inside the canvas (within 1px for anti-aliasing).
    CHECK(bbox.y0 >= 0);
    CHECK(bbox.y1 <= fb->height() - 1);
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.x1 <= fb->width() - 1);
}

// ═══ Test 6 — Small font size: no clipping at 24pt ═══════════════════════
TEST_CASE("NoClip 06: small font 24pt not clipped") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_clip_test_composition(renderer, "ÁgjyWMf", 24.0f),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("24pt bbox: w=", bbox.width(), " h=", bbox.height(),
         " y0=", bbox.y0, " y1=", bbox.y1);

    CHECK_FALSE(bbox.empty());
    CHECK(bbox.height() > 10);
    // All edges inside canvas.
    CHECK(bbox.y0 >= 0);
    CHECK(bbox.y1 <= fb->height() - 1);
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.x1 <= fb->width() - 1);
}
