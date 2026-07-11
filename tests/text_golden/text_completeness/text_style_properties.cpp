// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_style_properties.cpp
//
// P1-#9: Style Property Coverage — atomic verification that every text
// style property influences the rendered output.
//
// Properties under test:
//   1. font_size     → bbox grows proportionally
//   2. font_weight   → different weight changes ink pattern
//   3. font_style    → italic vs normal changes bbox shape
//   4. fill_color    → pixel colors reflect the set color
//   5. stroke        → stroke expands visible area
//   6. opacity       → layer opacity scales pixel alpha
//   7. tracking      → wider tracking increases text width
//   8. line_height   → inter-line spacing changes for multi-line text
//   9. box_size      → narrower box produces more wrapped lines
//  10. position      → position change moves ink centroid
//
// Design principle: each test compares TWO renders — baseline vs
// modified property — and verifies a measurable difference.
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

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

/// Baseline composition: centered white text on a 1920×1080 canvas
/// at 96pt Inter-Bold.
Composition build_baseline(SoftwareRenderer& renderer,
                           std::string_view text = "STYLE",
                           float font_size = 96.0f) {
    return composition(
        {.name = "TextStyle/baseline",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_size](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("style_layer", [&renderer, text, font_size](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("style_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = std::string{text}}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = font_size
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with a specific font_weight override.
Composition build_weight(SoftwareRenderer& renderer, int weight) {
    return composition(
        {.name = "TextStyle/weight",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, weight](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("w_layer", [&renderer, weight](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("weight_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "WEIGHT"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = weight,
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with a specific font_style, using a font that supports italic.
Composition build_style(SoftwareRenderer& renderer, std::string_view font_style) {
    // Use Inter-Regular for italic test — it has an actual italic face.
    const char* font_path = "assets/fonts/Inter-Regular.ttf";
    return composition(
        {.name = "TextStyle/fontStyle",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, font_style, font_path](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("fs_layer", [&renderer, font_style, font_path](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("font_style_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "STYLE"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = font_path,
                            .font_family = "Inter",
                            .font_weight = 400,
                            .font_style = std::string{font_style},
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with a specific fill_color.
Composition build_fill(SoftwareRenderer& renderer, Color color) {
    return composition(
        {.name = "TextStyle/fill",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, color](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("fill_layer", [&renderer, color](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("fill_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "COLOR"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = color}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with stroke.
Composition build_stroke(SoftwareRenderer& renderer, bool stroke_on, float stroke_w = 4.0f) {
    TextPaint paint;
    paint.fill = Color::white();
    paint.stroke_enabled = stroke_on;
    paint.stroke_color = Color{1.0f, 0.0f, 0.0f, 1.0f};  // red stroke
    paint.stroke_width = stroke_w;

    return composition(
        {.name = "TextStyle/stroke",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, paint](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("stroke_layer", [&renderer, paint](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("stroke_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "STROKE"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white(), .paint = paint}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with layer opacity.
Composition build_opacity(SoftwareRenderer& renderer, float opacity) {
    return composition(
        {.name = "TextStyle/opacity",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, opacity](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("opacity_layer", [&renderer, opacity](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.opacity(opacity);
                l.text_run("opacity_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "FADE"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with multi-line text and specific line_height.
Composition build_line_height(SoftwareRenderer& renderer, float lh) {
    return composition(
        {.name = "TextStyle/lineHeight",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, lh](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("lh_layer", [&renderer, lh](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("lh_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "LINE ONE\nLINE TWO\nLINE THREE"}, .placement = {TextPlacementKind::Absolute, {960.0f, 100.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 48.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Top,
                            .line_height = lh
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}
Composition build_tracking(SoftwareRenderer& renderer, float tracking) {
    return composition(
        {.name = "TextStyle/tracking",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, tracking](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("track_layer", [&renderer, tracking](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("track_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "TRACKING"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 72.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle,
                            .tracking = tracking
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with a layout box of specific width, for wrapping tests.
Composition build_wrap_box(SoftwareRenderer& renderer, float box_width) {
    return composition(
        {.name = "TextStyle/wrapBox",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, box_width](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("wrap_box_layer", [&renderer, box_width](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("wrap_box_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "The quick brown fox jumps over the lazy dog"}, .placement = {TextPlacementKind::Absolute, {box_width / 2.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 48.0f
                        }, .layout = {
                            .box = {box_width, 1080.0f},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Top
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

/// Composition with a specific anchor.
Composition build_anchor(SoftwareRenderer& renderer, TextAnchor anchor) {
    return composition(
        {.name = "TextStyle/anchor",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, anchor](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("anchor_layer", [&renderer, anchor](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("anchor_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "ANCHOR"}, .placement = {TextPlacementKind::Absolute, {960.0f, 540.0f}}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 64.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .anchor = anchor,
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Top
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

// ═══ Test 13 — Anchor: different anchors move ink centroid ═══════════════
TEST_CASE("TextStyle 13: different anchors shift ink position") {
    auto r_center = test::make_renderer();
    auto r_top_left = test::make_renderer();

    auto fb_c = r_center.render(
        build_anchor(r_center, TextAnchor::Center), Frame{0});
    auto fb_tl = r_top_left.render(
        build_anchor(r_top_left, TextAnchor::TopLeft), Frame{0});
    REQUIRE(fb_c != nullptr);
    REQUIRE(fb_tl != nullptr);

    const AlphaBBox bc = alpha_bbox(*fb_c);
    const AlphaBBox btl = alpha_bbox(*fb_tl);

    CHECK_FALSE(bc.empty());
    CHECK_FALSE(btl.empty());

    const int cy_center = (bc.y0 + bc.y1) / 2;
    const int cy_topleft = (btl.y0 + btl.y1) / 2;
    const int cx_center = (bc.x0 + bc.x1) / 2;
    const int cx_topleft = (btl.x0 + btl.x1) / 2;

    INFO("Center anchor: cx=", cx_center, " cy=", cy_center);
    INFO("TopLeft anchor: cx=", cx_topleft, " cy=", cy_topleft);

    // TopLeft anchor should place text higher (closer to top)
    // and more left than Center anchor.
    CHECK(cy_topleft < cy_center);
    CHECK(cx_topleft < cx_center);
}
Composition build_position(SoftwareRenderer& renderer, Vec3 pos) {
    return composition(
        {.name = "TextStyle/position",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, pos](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("pos_layer", [&renderer, pos](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("pos_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "POS"}, .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        }, .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        }, .appearance = {.color = Color::white()}}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Font size: bbox grows with larger size ═════════════════════
TEST_CASE("TextStyle 01: larger font_size produces larger bbox") {
    auto r1 = test::make_renderer();
    auto r2 = test::make_renderer();

    auto fb48 = r1.render(build_baseline(r1, "SIZE", 48.0f), Frame{0});
    auto fb96 = r2.render(build_baseline(r2, "SIZE", 96.0f), Frame{0});
    REQUIRE(fb48 != nullptr);
    REQUIRE(fb96 != nullptr);

    const AlphaBBox b48 = alpha_bbox(*fb48);
    const AlphaBBox b96 = alpha_bbox(*fb96);

    CHECK_FALSE(b48.empty());
    CHECK_FALSE(b96.empty());

    INFO("48pt: w=", b48.width(), " h=", b48.height());
    INFO("96pt: w=", b96.width(), " h=", b96.height());

    // Larger font → wider bbox.
    CHECK(b96.width() > b48.width());
    // Larger font → taller bbox.
    CHECK(b96.height() > b48.height());
    // Roughly proportional: double size → at least 1.5× dimensions.
    CHECK(b96.width() > b48.width() * 1.5f);
    CHECK(b96.height() > b48.height() * 1.5f);
}

// ═══ Test 2 — Font weight: different weights change ink pattern ══════════
TEST_CASE("TextStyle 02: different font_weight produces different ink") {
    auto r400 = test::make_renderer();
    auto r700 = test::make_renderer();

    auto fb400 = r400.render(build_weight(r400, 400), Frame{0});
    auto fb700 = r700.render(build_weight(r700, 700), Frame{0});
    REQUIRE(fb400 != nullptr);
    REQUIRE(fb700 != nullptr);

    const int vis400 = count_visible_pixels(*fb400);
    const int vis700 = count_visible_pixels(*fb700);

    INFO("weight=400 visible=", vis400);
    INFO("weight=700 visible=", vis700);

    CHECK(vis400 > 100);
    CHECK(vis700 > 100);
    // Bold (700) should have more ink coverage than normal (400).
    CHECK(vis700 > vis400);
}

// ═══ Test 3 — Font style: italic changes bbox shape ══════════════════════
TEST_CASE("TextStyle 03: italic font_style changes bbox width") {
    auto r_normal = test::make_renderer();
    auto r_italic = test::make_renderer();

    auto fb_n = r_normal.render(build_style(r_normal, "normal"), Frame{0});
    auto fb_i = r_italic.render(build_style(r_italic, "italic"), Frame{0});
    REQUIRE(fb_n != nullptr);
    REQUIRE(fb_i != nullptr);

    const AlphaBBox bn = alpha_bbox(*fb_n);
    const AlphaBBox bi = alpha_bbox(*fb_i);

    INFO("normal: w=", bn.width(), " h=", bn.height());
    INFO("italic: w=", bi.width(), " h=", bi.height());

    CHECK_FALSE(bn.empty());
    CHECK_FALSE(bi.empty());
    CHECK(count_visible_pixels(*fb_n) > 100);
    CHECK(count_visible_pixels(*fb_i) > 100);

    // Italic slant should produce a measurably different (wider) bbox.
    // Compare bbox widths — if the engine respects font_style, italic
    // will have a different width than normal.
    CHECK(bi.width() != bn.width());
}

// ═══ Test 4 — Fill color: pixel colors reflect the set color ═════════════
TEST_CASE("TextStyle 04: fill_color changes pixel RGB") {
    auto r_red = test::make_renderer();
    auto r_blue = test::make_renderer();

    auto fb_red = r_red.render(
        build_fill(r_red, Color{1.0f, 0.0f, 0.0f, 1.0f}), Frame{0});
    auto fb_blue = r_blue.render(
        build_fill(r_blue, Color{0.0f, 0.0f, 1.0f, 1.0f}), Frame{0});
    REQUIRE(fb_red != nullptr);
    REQUIRE(fb_blue != nullptr);

    // Compute mean red and blue channels for both framebuffers.
    double sum_red_r = 0.0, sum_red_b = 0.0;
    double sum_blue_r = 0.0, sum_blue_b = 0.0;
    const int total = fb_red->width() * fb_red->height();

    for (int y = 0; y < fb_red->height(); ++y) {
        const Color* row_r = fb_red->pixels_row(y);
        const Color* row_b = fb_blue->pixels_row(y);
        for (int x = 0; x < fb_red->width(); ++x) {
            sum_red_r += row_r[x].r;
            sum_red_b += row_r[x].b;
            sum_blue_r += row_b[x].r;
            sum_blue_b += row_b[x].b;
        }
    }
    const float mean_red_r = static_cast<float>(sum_red_r / total);
    const float mean_red_b = static_cast<float>(sum_red_b / total);
    const float mean_blue_r = static_cast<float>(sum_blue_r / total);
    const float mean_blue_b = static_cast<float>(sum_blue_b / total);

    INFO("red fill: mean_r=", mean_red_r, " mean_b=", mean_red_b);
    INFO("blue fill: mean_r=", mean_blue_r, " mean_b=", mean_blue_b);

    // Red fill → higher red channel.
    CHECK(mean_red_r > mean_blue_r);
    // Blue fill → higher blue channel.
    CHECK(mean_blue_b > mean_red_b);
}

// ═══ Test 5 — Stroke: stroke expands visible area ════════════════════════
TEST_CASE("TextStyle 05: stroke increases visible pixel count") {
    auto r_no = test::make_renderer();
    auto r_yes = test::make_renderer();

    auto fb_no = r_no.render(build_stroke(r_no, false), Frame{0});
    auto fb_yes = r_yes.render(build_stroke(r_yes, true, 6.0f), Frame{0});
    REQUIRE(fb_no != nullptr);
    REQUIRE(fb_yes != nullptr);

    const int vis_no = count_visible_pixels(*fb_no);
    const int vis_yes = count_visible_pixels(*fb_yes);

    INFO("no stroke visible=", vis_no);
    INFO("stroke 6px visible=", vis_yes);

    CHECK(vis_no > 100);
    // Stroke adds ink around the glyphs.
    CHECK(vis_yes > vis_no);
}

// ═══ Test 6 — Opacity: lower opacity reduces mean alpha ══════════════════
TEST_CASE("TextStyle 06: lower opacity reduces mean alpha") {
    auto r_full = test::make_renderer();
    auto r_half = test::make_renderer();

    auto fb_full = r_full.render(build_opacity(r_full, 1.0f), Frame{0});
    auto fb_half = r_half.render(build_opacity(r_half, 0.5f), Frame{0});
    REQUIRE(fb_full != nullptr);
    REQUIRE(fb_half != nullptr);

    const float alpha_full = mean_alpha(*fb_full);
    const float alpha_half = mean_alpha(*fb_half);

    INFO("opacity=1.0 mean_alpha=", alpha_full);
    INFO("opacity=0.5 mean_alpha=", alpha_half);

    CHECK(alpha_full > 0.0f);
    // Half opacity → roughly half the mean alpha (at least noticeably lower).
    CHECK(alpha_half < alpha_full * 0.8f);
}

// ═══ Test 7 — Tracking: wider tracking increases text width ══════════════
TEST_CASE("TextStyle 07: wider tracking produces wider bbox") {
    auto r_tight = test::make_renderer();
    auto r_wide = test::make_renderer();

    auto fb_tight = r_tight.render(build_tracking(r_tight, 0.0f), Frame{0});
    auto fb_wide = r_wide.render(build_tracking(r_wide, 20.0f), Frame{0});
    REQUIRE(fb_tight != nullptr);
    REQUIRE(fb_wide != nullptr);

    const AlphaBBox bt = alpha_bbox(*fb_tight);
    const AlphaBBox bw = alpha_bbox(*fb_wide);

    INFO("tracking=0: w=", bt.width());
    INFO("tracking=20: w=", bw.width());

    CHECK_FALSE(bt.empty());
    CHECK_FALSE(bw.empty());
    // Wider tracking → wider bbox.
    CHECK(bw.width() > bt.width());
    // With 20px extra per character on "TRACKING" (8 chars),
    // width should increase significantly.
    CHECK(bw.width() > bt.width() + 40);
}

// ═══ Test 8 — Line height: multi-line spacing changes with line_height ═══
TEST_CASE("TextStyle 08: larger line_height increases multi-line vertical extent") {
    auto r_tight = test::make_renderer();
    auto r_loose = test::make_renderer();

    auto fb_tight = r_tight.render(build_line_height(r_tight, 1.0f), Frame{0});
    auto fb_loose = r_loose.render(build_line_height(r_loose, 2.5f), Frame{0});
    REQUIRE(fb_tight != nullptr);
    REQUIRE(fb_loose != nullptr);

    const auto [tt, bt] = ink_vertical_extent(*fb_tight);
    const auto [tl, bl] = ink_vertical_extent(*fb_loose);
    const int h_tight = (tt >= 0) ? (bt - tt + 1) : 0;
    const int h_loose = (tl >= 0) ? (bl - tl + 1) : 0;

    INFO("line_height=1.0: extent=", h_tight, " top=", tt, " bot=", bt);
    INFO("line_height=2.5: extent=", h_loose, " top=", tl, " bot=", bl);

    CHECK(count_visible_pixels(*fb_tight) > 100);
    CHECK(count_visible_pixels(*fb_loose) > 100);
    // Larger line_height → more vertical extent (lines spread apart).
    CHECK(h_loose > h_tight);
    // With 3 lines at line_height=2.5, the extent should be at least
    // 50% larger than line_height=1.0.
    CHECK(h_loose > h_tight * 1.5f);
}

// ═══ Test 9 — Box size: narrower box produces more wrapped lines ═════════
TEST_CASE("TextStyle 09: narrower box has more ink rows") {
    auto r_wide = test::make_renderer();
    auto r_narrow = test::make_renderer();

    auto fb_wide = r_wide.render(build_wrap_box(r_wide, 1200.0f), Frame{0});
    auto fb_narrow = r_narrow.render(build_wrap_box(r_narrow, 350.0f), Frame{0});
    REQUIRE(fb_wide != nullptr);
    REQUIRE(fb_narrow != nullptr);

    const int rows_wide = count_ink_rows(*fb_wide);
    const int rows_narrow = count_ink_rows(*fb_narrow);
    const auto [tw, bw] = ink_vertical_extent(*fb_wide);
    const auto [tn, bn] = ink_vertical_extent(*fb_narrow);
    const int h_wide = (tw >= 0) ? (bw - tw + 1) : 0;
    const int h_narrow = (tn >= 0) ? (bn - tn + 1) : 0;

    INFO("wide box (1200): rows=", rows_wide, " height=", h_wide);
    INFO("narrow box (350): rows=", rows_narrow, " height=", h_narrow);

    CHECK(count_visible_pixels(*fb_wide) > 100);
    CHECK(count_visible_pixels(*fb_narrow) > 100);
    // Narrower box → more wrapping → more ink rows.
    CHECK(rows_narrow > rows_wide);
    CHECK(h_narrow > h_wide);
}

// ═══ Test 10 — Position: moving position shifts ink centroid ═════════════
TEST_CASE("TextStyle 10: position change moves ink centroid") {
    auto r_left = test::make_renderer();
    auto r_right = test::make_renderer();

    auto fb_left = r_left.render(
        build_position(r_left, Vec3{400.0f, 540.0f, 0.0f}), Frame{0});
    auto fb_right = r_right.render(
        build_position(r_right, Vec3{1500.0f, 540.0f, 0.0f}), Frame{0});
    REQUIRE(fb_left != nullptr);
    REQUIRE(fb_right != nullptr);

    const AlphaBBox bl = alpha_bbox(*fb_left);
    const AlphaBBox br = alpha_bbox(*fb_right);

    CHECK_FALSE(bl.empty());
    CHECK_FALSE(br.empty());

    const int cx_left = (bl.x0 + bl.x1) / 2;
    const int cx_right = (br.x0 + br.x1) / 2;

    INFO("left pos(400): cx=", cx_left);
    INFO("right pos(1500): cx=", cx_right);

    CHECK(cx_right > cx_left + 400);
}

// ═══ Test 11 — Large font_size: extreme size doesn't crash ═══════════════
TEST_CASE("TextStyle 11: extreme font_size 300pt does not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_baseline(renderer, "BIG", 300.0f), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("300pt: visible=", visible, " w=", bbox.width(), " h=", bbox.height());

    CHECK(visible > 100);
    CHECK_FALSE(bbox.empty());
}

// ═══ Test 12 — Small font_size: tiny size still renders ══════════════════
TEST_CASE("TextStyle 12: tiny font_size 8pt does not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_baseline(renderer, "tiny", 8.0f), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("8pt: visible=", visible);
    // 8pt text on a 1920×1080 canvas — should have some ink, even if tiny.
    CHECK(visible > 0);
}
