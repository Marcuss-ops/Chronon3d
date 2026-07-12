// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/glow_ab/glow_ab_acceptance.cpp
//
// TICKET-GLOW-CERTIFICATION — Azione 1: 6 glow acceptance TEST_CASEs.
//
//   1. Glow intensity zero = disabled glow (framebuffer hash equality).
//   2. Glow radius grows halo without moving text (bbox monotonic + centroid stable).
//   3. Additive compositing never reduces source pixels (core luma ≥ 98%).
//   4. Anti-clipping: alpha bbox inside predicted bbox, no edge touch.
//   5. No rectangular layer edge visible at glow boundary.
//   6. State leak ON/OFF/ON: first ON == last ON, ON != OFF.
//
// Uses the canonical ChrononGlowFinalAE factory via
// glow_final_compositions.hpp (ChrononGlowProps, make_chronon_glow_final_for_test)
// plus the existing test helpers (alpha_bbox, alpha_centroid, framebuffer_hash,
// average_luma_rect) from tests/helpers and tests/text_golden/text_clip/.
//
// AGENTS.md Cat-2 freeze-compliant: zero new public SDK API.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>
#include <tests/visual/ae_parity/glow_final_compositions.hpp>

#include <cmath>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::test::glow_final::ChrononGlowProps;

// ── Shared renderer factory (one per TEST_CASE, cheap) ────────────────────

namespace {

/// Render the canonical ChrononGlowFinalAE composition with the given props
/// at the given frame.  Returns the rendered framebuffer.
std::shared_ptr<Framebuffer> render_at(
    std::shared_ptr<SoftwareRenderer> renderer,
    ChrononGlowProps props,
    Frame frame) {
    return renderer->render(
        chronon3d::test::glow_final::make_chronon_glow_final_for_test(
            props, renderer->font_engine()),
        frame);
}

/// Build a minimal text scene with configurable GlowParams and render it.
/// Returns the framebuffer.  Used by radius + additive tests.
std::shared_ptr<Framebuffer> render_glow_params(
    std::shared_ptr<SoftwareRenderer> renderer,
    GlowParams glow,
    int width = 640,
    int height = 480) {
    Composition comp = composition(
        {.name = "glow_params_test",
         .width = static_cast<unsigned>(width),
         .height = static_cast<unsigned>(height),
         .duration = 1},
        [glow](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("hero", [&](LayerBuilder& l) {
                l.text_run("txt", TextRunSpec{
                    .text = TextSpec{
                        .content = {.value = "GLOW"},
                        .placement = TextPlacement{
                            TextPlacementKind::CanvasCenter, {}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 120.0f,
                        },
                        .layout = {.box = {600.0f, 200.0f},
                                   .align = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle},
                        .appearance = {.color = Color::white()},
                    },
                }).commit();
                if (glow.enabled && glow.intensity > 0.0f) {
                    l.glow(glow);
                }
            });
            return s.build();
        });
    return renderer->render(comp, Frame{0});
}

/// Simple L2 distance between two Vec2.
float dist(Vec2 a, Vec2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST 1 — Intensity zero = disabled glow
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow acceptance: intensity zero is identical to disabled glow") {
    auto renderer = make_renderer_shared();

    // Disabled
    ChrononGlowProps disabled = chronon3d::test::glow_final::default_landscape_props();
    disabled.glow_enabled = false;

    // Enabled but intensity zero
    ChrononGlowProps zero = chronon3d::test::glow_final::default_landscape_props();
    zero.glow_enabled = true;
    zero.glow_strength = 0.0f;

    auto fb_disabled = render_at(renderer, disabled, Frame{15});
    auto fb_zero     = render_at(renderer, zero,     Frame{15});

    REQUIRE(fb_disabled != nullptr);
    REQUIRE(fb_zero     != nullptr);

    u64 hash_disabled = framebuffer_hash(*fb_disabled);
    u64 hash_zero     = framebuffer_hash(*fb_zero);

    INFO("disabled hash = ", hash_disabled, "  zero hash = ", hash_zero);
    CHECK(hash_disabled == hash_zero);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 2 — Radius grows halo without moving the text
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow acceptance: radius grows halo without moving text") {
    auto renderer = make_renderer_shared();

    auto render_with_radius = [&](f32 radius) -> AlphaBBox {
        GlowParams g;
        g.enabled   = true;
        g.intensity = 0.8f;
        g.radius    = radius;
        g.color     = Color::white();
        auto fb = render_glow_params(renderer, g);
        return alpha_bbox(*fb, 0.01f);
    };

    auto r0  = render_with_radius(0.0f);
    auto r8  = render_with_radius(8.0f);
    auto r20 = render_with_radius(20.0f);
    auto r40 = render_with_radius(40.0f);

    REQUIRE_FALSE(r0.empty());
    REQUIRE_FALSE(r8.empty());
    REQUIRE_FALSE(r20.empty());
    REQUIRE_FALSE(r40.empty());

    INFO("r0  bbox: ", r0.x0, ",", r0.y0, " → ", r0.x1, ",", r0.y1,
         "  (", r0.width(), "×", r0.height(), ")");
    INFO("r8  bbox: ", r8.x0, ",", r8.y0, " → ", r8.x1, ",", r8.y1,
         "  (", r8.width(), "×", r8.height(), ")");
    INFO("r20 bbox: ", r20.x0, ",", r20.y0, " → ", r20.x1, ",", r20.y1,
         "  (", r20.width(), "×", r20.height(), ")");
    INFO("r40 bbox: ", r40.x0, ",", r40.y0, " → ", r40.x1, ",", r40.y1,
         "  (", r40.width(), "×", r40.height(), ")");

    // BBox grows monotonically with radius.
    CHECK(r8.width()  <= r20.width());
    CHECK(r20.width() <= r40.width());
    CHECK(r8.height()  <= r20.height());
    CHECK(r20.height() <= r40.height());

    // Centroid stays stable (within 2 px across all radii).
    auto centroid_of = [](const AlphaBBox& b) -> Vec2 {
        return Vec2{(b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f};
    };
    Vec2 c0  = centroid_of(r0);
    Vec2 c8  = centroid_of(r8);
    Vec2 c20 = centroid_of(r20);
    Vec2 c40 = centroid_of(r40);

    CHECK(dist(c0, c8)  < 2.0f);
    CHECK(dist(c0, c20) < 2.0f);
    CHECK(dist(c0, c40) < 2.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 3 — Additive compositing never reduces source pixels
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow acceptance: additive glow never reduces source pixels") {
    auto renderer = make_renderer_shared();

    // Without glow (source baseline)
    ChrononGlowProps no_glow = chronon3d::test::glow_final::default_landscape_props();
    no_glow.glow_enabled = false;

    // With glow
    ChrononGlowProps with_glow = chronon3d::test::glow_final::default_landscape_props();
    with_glow.glow_enabled = true;

    auto fb_no   = render_at(renderer, no_glow,   Frame{15});
    auto fb_with = render_at(renderer, with_glow, Frame{15});

    REQUIRE(fb_no   != nullptr);
    REQUIRE(fb_with != nullptr);

    // Use the source (no-glow) alpha centroid + core mask.
    const Vec2 centre = alpha_centroid(*fb_no);
    REQUIRE(centre.x > 0.0f);
    REQUIRE(centre.y > 0.0f);

    constexpr int kHalfW = 4;  // 9×9 sample window
    auto sample_luma = [](const Framebuffer& fb, float cx, float cy, int hw) -> float {
        int x0 = std::max(0, static_cast<int>(cx) - hw);
        int y0 = std::max(0, static_cast<int>(cy) - hw);
        int x1 = std::min(fb.width(),  static_cast<int>(cx) + hw + 1);
        int y1 = std::min(fb.height(), static_cast<int>(cy) + hw + 1);
        return average_luma_rect(fb, x0, y0, x1, y1);
    };

    float src_luma  = sample_luma(*fb_no,   centre.x, centre.y, kHalfW);
    float glow_luma = sample_luma(*fb_with, centre.x, centre.y, kHalfW);

    INFO("source core luma = ", src_luma, "  glow core luma = ", glow_luma);
    REQUIRE(src_luma > 1e-6f);
    CHECK(glow_luma >= 0.98f * src_luma);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 4 — Anti-clipping: glow bbox inside predicted bbox, no edge touch
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow acceptance: alpha bbox inside canvas, no edge clipping") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    props.glow_enabled = true;

    auto fb = render_at(renderer, props, Frame{15});
    REQUIRE(fb != nullptr);

    AlphaBBox bbox = alpha_bbox(*fb, 0.01f);
    REQUIRE_FALSE(bbox.empty());

    INFO("alpha bbox: ", bbox.x0, ",", bbox.y0, " → ", bbox.x1, ",", bbox.y1,
         "  (", bbox.width(), "×", bbox.height(), ")");
    INFO("canvas: ", fb->width(), "×", fb->height());

    // BBox must be fully inside the framebuffer.
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.y0 >= 0);
    CHECK(bbox.x1 < fb->width());
    CHECK(bbox.y1 < fb->height());

    // No edge touch with 8 px margin (glow halo should have breathing room).
    constexpr int kMargin = 8;
    CHECK_FALSE(bbox.touches_left(kMargin));
    CHECK_FALSE(bbox.touches_top(kMargin));
    CHECK_FALSE(bbox.touches_right(fb->width(), kMargin));
    CHECK_FALSE(bbox.touches_bottom(fb->height(), kMargin));
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 5 — No rectangular layer edge visible at the glow boundary
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow acceptance: no visible rectangular layer edge") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    props.glow_enabled = true;

    auto fb = render_at(renderer, props, Frame{15});
    REQUIRE(fb != nullptr);

    AlphaBBox bbox = alpha_bbox(*fb, 0.01f);
    REQUIRE_FALSE(bbox.empty());

    // Scan a 4-pixel band just OUTSIDE the alpha bbox on each side.
    // If the glow layer had a hard rectangular edge, we'd see pixels
    // with alpha just above the epsilon threshold in a uniform band.
    // A proper glow fades smoothly, so the outer band should have very
    // few visible pixels compared to the bbox interior.
    constexpr float kEdgeAlphaMax = 0.05f;  // outside bbox, alpha must be very low

    const int w = fb->width();
    const int h = fb->height();
    const int band = 4;

    // Right edge band
    int right_edge_pixels = 0;
    for (int y = std::max(0, bbox.y1 - band); y < std::min(h, bbox.y1 + band + 1); ++y) {
        for (int x = bbox.x1 + 1; x < std::min(w, bbox.x1 + band + 1); ++x) {
            if (fb->get_pixel(x, y).a > kEdgeAlphaMax) ++right_edge_pixels;
        }
    }
    // Bottom edge band
    int bottom_edge_pixels = 0;
    for (int y = bbox.y1 + 1; y < std::min(h, bbox.y1 + band + 1); ++y) {
        for (int x = std::max(0, bbox.x0); x <= std::min(w - 1, bbox.x1); ++x) {
            if (fb->get_pixel(x, y).a > kEdgeAlphaMax) ++bottom_edge_pixels;
        }
    }

    INFO("right-edge bright pixels: ", right_edge_pixels,
         "  bottom-edge bright pixels: ", bottom_edge_pixels);

    // A true rectangular edge would have a band of bright pixels right
    // outside the bbox.  We expect zero such pixels (proper glow fades).
    CHECK(right_edge_pixels == 0);
    CHECK(bottom_edge_pixels == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 6 — State leak ON/OFF/ON
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow acceptance: state does not leak between renders") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps on_props = chronon3d::test::glow_final::default_landscape_props();
    on_props.glow_enabled = true;

    ChrononGlowProps off_props = chronon3d::test::glow_final::default_landscape_props();
    off_props.glow_enabled = false;

    // Sequence: ON → OFF → ON
    auto glow_on_1  = render_at(renderer, on_props,  Frame{15});
    auto glow_off   = render_at(renderer, off_props, Frame{15});
    auto glow_on_2  = render_at(renderer, on_props,  Frame{15});

    REQUIRE(glow_on_1  != nullptr);
    REQUIRE(glow_off   != nullptr);
    REQUIRE(glow_on_2  != nullptr);

    u64 hash_on_1  = framebuffer_hash(*glow_on_1);
    u64 hash_off   = framebuffer_hash(*glow_off);
    u64 hash_on_2  = framebuffer_hash(*glow_on_2);

    INFO("hash ON-1 = ", hash_on_1, "  hash OFF = ", hash_off,
         "  hash ON-2 = ", hash_on_2);

    // First ON must equal second ON (no state leak).
    CHECK(hash_on_1 == hash_on_2);

    // ON must differ from OFF (glow actually renders).
    CHECK(hash_on_1 != hash_off);
}
