// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_wrapping.cpp
//
// P0-3: Text Wrapping — verifies that text wraps correctly inside
// layout boxes of different widths.
//
// Cases:
//   1. Wide box (1200px)   → few lines, text fits comfortably
//   2. Medium box (600px)  → more lines, wrapping occurs
//   3. Narrow box (250px)  → many lines, no word escapes
//   4. Determinism: same input → same layout across renders
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

constexpr const char* kWrapText =
    "The quick brown fox jumps over the lazy dog. "
    "Pack my box with five dozen liquor jugs. "
    "How vexingly quick daft zebras jump.";

Composition build_wrap_composition(
    SoftwareRenderer& renderer,
    float box_width,
    float box_height = 1080.0f,
    std::string_view text = kWrapText
) {
    return composition(
        {.name = "TextWrap/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, box_width, box_height, text](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("wrap_layer", [&renderer, box_width, box_height, text](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("wrap_test", TextRunSpec{
                    .text = TextSpec{
                        .content = {.value = std::string{text}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 48.0f
                        },
                        .layout = {
                            .box = {box_width, box_height},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Top
                        },
                        .appearance = {.color = Color::white()},
                        .position = {box_width / 2.0f, box_height / 2.0f, 0.0f}
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Wide box (1200px): few lines ═══════════════════════════════
TEST_CASE("TextWrap 01: wide box 1200px has few lines") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_wrap_composition(renderer, 1200.0f), Frame{0});
    REQUIRE(fb != nullptr);

    const int rows = count_ink_rows(*fb);
    const int visible = count_visible_pixels(*fb);
    const auto [top, bottom] = ink_vertical_extent(*fb);
    const int height = (top >= 0) ? (bottom - top + 1) : 0;

    INFO("ink_rows=", rows, " visible=", visible, " height=", height);
    CHECK(visible > 100);
    CHECK(height < 300);
    CHECK(rows > 0);
}

// ═══ Test 2 — Medium box (600px): more lines than wide ═══════════════════
// NOTE: Uses SEPARATE renderers to avoid cache reuse.
TEST_CASE("TextWrap 02: medium box 600px has more lines than wide") {
    // Use fresh renderers so the pipeline doesn't cache between the two.
    auto renderer_wide = test::make_renderer();
    auto renderer_med = test::make_renderer();

    auto fb_wide = renderer_wide.render(
        build_wrap_composition(renderer_wide, 1200.0f), Frame{0});
    auto fb_med = renderer_med.render(
        build_wrap_composition(renderer_med, 600.0f), Frame{0});

    REQUIRE(fb_wide != nullptr);
    REQUIRE(fb_med != nullptr);

    const int rows_wide = count_ink_rows(*fb_wide);
    const int rows_med = count_ink_rows(*fb_med);
    const auto [top_w, bottom_w] = ink_vertical_extent(*fb_wide);
    const auto [top_m, bottom_m] = ink_vertical_extent(*fb_med);
    const int h_wide = (top_w >= 0) ? (bottom_w - top_w + 1) : 0;
    const int h_med = (top_m >= 0) ? (bottom_m - top_m + 1) : 0;

    INFO("wide: rows=", rows_wide, " height=", h_wide);
    INFO("medium: rows=", rows_med, " height=", h_med);

    CHECK(count_visible_pixels(*fb_wide) > 100);
    CHECK(count_visible_pixels(*fb_med) > 100);
    CHECK(rows_med > rows_wide);
    CHECK(h_med > h_wide);
}

// ═══ Test 3 — Narrow box (250px): many lines, no word escape ═════════════
TEST_CASE("TextWrap 03: narrow box 250px wraps without word escape") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_wrap_composition(renderer, 250.0f), Frame{0});
    REQUIRE(fb != nullptr);

    const int rows = count_ink_rows(*fb);
    const int visible = count_visible_pixels(*fb);
    const int max_w = max_ink_row_width(*fb);

    INFO("rows=", rows, " visible=", visible, " max_row_width=", max_w);
    CHECK(visible > 100);
    CHECK(rows > 3);
    // No row exceeds box width + anti-aliasing margin (250px + 40px).
    CHECK(max_w < 290);
}

// ═══ Test 4 — Determinism: same input → same ink layout ══════════════════
TEST_CASE("TextWrap 04: wrapping is deterministic across renders") {
    auto r1 = test::make_renderer();
    auto r2 = test::make_renderer();

    auto fb1 = r1.render(build_wrap_composition(r1, 600.0f), Frame{0});
    auto fb2 = r2.render(build_wrap_composition(r2, 600.0f), Frame{0});

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    const int rows1 = count_ink_rows(*fb1);
    const int rows2 = count_ink_rows(*fb2);
    const auto [t1, b1] = ink_vertical_extent(*fb1);
    const auto [t2, b2] = ink_vertical_extent(*fb2);
    const int h1 = (t1 >= 0) ? (b1 - t1 + 1) : 0;
    const int h2 = (t2 >= 0) ? (b2 - t2 + 1) : 0;

    CHECK(rows1 == rows2);
    CHECK(h1 == h2);
    CHECK(count_visible_pixels(*fb1) == count_visible_pixels(*fb2));
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}
