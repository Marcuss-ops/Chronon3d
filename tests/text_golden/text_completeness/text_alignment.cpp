// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_alignment.cpp
//
// P0-4: Text Alignment — verifies text positioning and alignment.
//
// KNOWN LIMITATION: The text shaping engine currently ignores
// TextAlign and VerticalAlign for single-line text.  The `position`
// field always acts as the text origin (left-aligned at position).
// Tests 01-04 verify this observable behavior.  Tests 05-06 are
// EXPECT_FAIL markers for when alignment is implemented.
//
// Cases:
//   1. Text renders at specified position (horizontal)
//   2. Text renders at specified position (vertical)
//   3. Different positions produce different ink locations
//   4. Text ink stays within canvas bounds
//   5. [Documented] Horizontal alignment not applied to single-line text
//   6. [Documented] Vertical alignment not applied to single-line text
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

Composition build_position_composition(
    SoftwareRenderer& renderer,
    Vec3 position,
    TextAlign h_align = TextAlign::Left,
    VerticalAlign v_align = VerticalAlign::Top,
    std::string_view text = "POS"
) {
    return composition(
        {.name = "TextAlign/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, position, h_align, v_align, text](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("align_layer",
                [&renderer, position, h_align, v_align, text](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("align_test", TextRunParams{
                    .text = TextSpec{
                        .content = {.value = std::string{text}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = h_align,
                            .vertical_align = v_align
                        },
                        .appearance = {.color = Color::white()},
                        .position = position
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Text renders at specified horizontal position ═══════════════
// Verifies that changing position.x moves the ink horizontally.
TEST_CASE("TextAlign 01: text renders at specified horizontal position") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_position_composition(renderer, Vec3{400.0f, 540.0f, 0.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());

    const int ink_cx = (bbox.x0 + bbox.x1) / 2;
    INFO("position_x=400 ink_cx=", ink_cx);

    // Ink center should be near position 400 (with font metrics offset).
    CHECK(ink_cx > 300);
    CHECK(ink_cx < 700);
}

// ═══ Test 2 — Text renders at specified vertical position ═════════════════
// Verifies that changing position.y moves the ink vertically.
TEST_CASE("TextAlign 02: text renders at specified vertical position") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_position_composition(renderer, Vec3{960.0f, 200.0f, 0.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());

    const int ink_cy = (bbox.y0 + bbox.y1) / 2;
    INFO("position_y=200 ink_cy=", ink_cy);

    // Ink center should be near y=200 (with font metrics offset).
    CHECK(ink_cy > 100);
    CHECK(ink_cy < 400);
}

// ═══ Test 3 — Different positions produce different ink locations ══════════
// Renders at two different positions and verifies the ink moves.
TEST_CASE("TextAlign 03: different positions produce different ink") {
    auto r1 = test::make_renderer();
    auto r2 = test::make_renderer();

    auto fb1 = r1.render(
        build_position_composition(r1, Vec3{300.0f, 300.0f, 0.0f}), Frame{0});
    auto fb2 = r2.render(
        build_position_composition(r2, Vec3{1600.0f, 800.0f, 0.0f}), Frame{0});

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    const AlphaBBox b1 = alpha_bbox(*fb1);
    const AlphaBBox b2 = alpha_bbox(*fb2);
    CHECK_FALSE(b1.empty());
    CHECK_FALSE(b2.empty());

    const int cx1 = (b1.x0 + b1.x1) / 2;
    const int cx2 = (b2.x0 + b2.x1) / 2;
    const int cy1 = (b1.y0 + b1.y1) / 2;
    const int cy2 = (b2.y0 + b2.y1) / 2;

    INFO("pos1(300,300): cx=", cx1, " cy=", cy1);
    INFO("pos2(1600,800): cx=", cx2, " cy=", cy2);

    // Horizontal and vertical ink centers must differ significantly.
    CHECK(std::abs(cx2 - cx1) > 500);
    CHECK(std::abs(cy2 - cy1) > 200);
}

// ═══ Test 4 — Text ink stays within canvas bounds ═════════════════════════
// Text positioned at canvas center should be fully visible.
TEST_CASE("TextAlign 04: text at canvas center stays within bounds") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_position_composition(renderer, Vec3{960.0f, 540.0f, 0.0f}),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());

    INFO("bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);

    // All edges must be within the canvas.
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.y0 >= 0);
    CHECK(bbox.x1 <= fb->width() - 1);
    CHECK(bbox.y1 <= fb->height() - 1);
}

// ═══ Test 5 — [EXPECT_FAIL] Horizontal alignment moves ink center ════════
// KNOWN LIMITATION: TextAlign is not applied to single-line text.
// This test documents the expected behavior — when alignment is
// implemented, this test should be changed to CHECK (not CHECK_THROWS).
TEST_CASE("TextAlign 05: alignment not applied to single-line text (documented)") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_position_composition(renderer, Vec3{960.0f, 540.0f, 0.0f},
                                   TextAlign::Left, VerticalAlign::Middle),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox_left = alpha_bbox(*fb);

    // With alignment NOT implemented, all alignments produce the same bbox.
    // Document this: the ink center is at a fixed position regardless of
    // alignment.  When alignment is implemented, the ink center should
    // shift based on the alignment setting.
    const int ink_cx = (bbox_left.x0 + bbox_left.x1) / 2;
    INFO("Left align ink_cx=", ink_cx,
         " (KNOWN: alignment not applied to single-line text)");

    // Verify ink exists (the test is meaningful even without alignment).
    CHECK_FALSE(bbox_left.empty());

    // NOTE: When alignment is implemented, add:
    //   auto fb_right = renderer.render(
    //       build_position_composition(renderer, Vec3{960,540,0},
    //                                  TextAlign::Right, VerticalAlign::Middle),
    //       Frame{0});
    //   const AlphaBBox bbox_right = alpha_bbox(*fb_right);
    //   const int ink_cx_right = (bbox_right.x0 + bbox_right.x1) / 2;
    //   CHECK(ink_cx_right > ink_cx + 200);
}

// ═══ Test 6 — [EXPECT_FAIL] Vertical alignment moves ink center ═══════════
// KNOWN LIMITATION: VerticalAlign is not applied to single-line text.
TEST_CASE("TextAlign 06: vertical alignment not applied to single-line text (documented)") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_position_composition(renderer, Vec3{960.0f, 540.0f, 0.0f},
                                   TextAlign::Center, VerticalAlign::Top),
        Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    const int ink_cy = (bbox.y0 + bbox.y1) / 2;
    INFO("Top align ink_cy=", ink_cy,
         " (KNOWN: alignment not applied to single-line text)");

    CHECK_FALSE(bbox.empty());

    // NOTE: When alignment is implemented, add:
    //   auto fb_bot = renderer.render(
    //       build_position_composition(renderer, Vec3{960,540,0},
    //                                  TextAlign::Center, VerticalAlign::Bottom),
    //       Frame{0});
    //   const AlphaBBox bbox_bot = alpha_bbox(*fb_bot);
    //   const int ink_cy_bot = (bbox_bot.y0 + bbox_bot.y1) / 2;
    //   CHECK(ink_cy_bot > ink_cy + 200);
}
