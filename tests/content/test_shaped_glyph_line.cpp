// tests/content/test_shaped_glyph_line.cpp
//
// Regression tests for content/common/text/glyph_layout.hpp::ShapedGlyphLine.
// Verifies that a single shaping produces width, per-glyph layout, cursor
// positions, bounding box and reveal counts.

#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include "content/common/text/glyph_layout.hpp"

#include <stdexcept>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

TEST_CASE("ShapedGlyphLine: width matches legacy measure_text_width") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line("Hello", 72.0f, spec, 4.0f, 0.0f, engine);

    f32 expected = content::text_reveal::measure_text_width("Hello", 72.0f, spec, 4.0f, engine);
    CHECK(line.width() == expected);
    CHECK(line.width() > 0.0f);
}

TEST_CASE("ShapedGlyphLine: layout matches legacy layout_glyphs") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line("Hello", 72.0f, spec, 4.0f, 0.0f, engine);

    auto glyphs = line.layout();
    auto expected = content::text_reveal::layout_glyphs("Hello", 72.0f, spec, 4.0f, 0.0f, engine);

    REQUIRE(glyphs.size() == expected.size());
    for (size_t i = 0; i < glyphs.size(); ++i) {
        CHECK(glyphs[i].ch == expected[i].ch);
        CHECK(glyphs[i].center_x == expected[i].center_x);
        CHECK(glyphs[i].width == expected[i].width);
    }
}

TEST_CASE("ShapedGlyphLine: cursor positions are monotonic and span the line") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line("ABC", 72.0f, spec, 4.0f, 10.0f, engine);

    CHECK(line.cursor_position(0) == 10.0f);
    CHECK(line.cursor_position(0) < line.cursor_position(1));
    CHECK(line.cursor_position(1) < line.cursor_position(2));
    CHECK(line.cursor_position(2) < line.cursor_position(3));
    CHECK(line.cursor_at_end() == line.cursor_position(3));
}

TEST_CASE("ShapedGlyphLine: bbox has non-negative width and height") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line("Hello", 72.0f, spec, 4.0f, 0.0f, engine);

    auto box = line.bbox();
    CHECK(box.width() >= 0.0f);
    CHECK(box.height() >= 0.0f);
    CHECK(box.x1 >= box.x0);
    CHECK(box.y1 >= box.y0);
}

TEST_CASE("ShapedGlyphLine: reveal_count is clamped to [0, glyph_count]") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line("Hello", 72.0f, spec, 4.0f, 0.0f, engine);

    CHECK(line.reveal_count(-0.5f) == 0);
    CHECK(line.reveal_count(0.0f) == 0);
    CHECK(line.reveal_count(1.0f) == line.layout().size());
    CHECK(line.reveal_count(2.0f) == line.layout().size());

    size_t mid = line.reveal_count(0.5f);
    CHECK(mid > 0);
    CHECK(mid <= line.layout().size());
}

TEST_CASE("ShapedGlyphLine: throws on non-existent font path") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec bad_spec{"assets/fonts/NonExistentFont.ttf", "Unknown", 400};

    CHECK_THROWS_AS(
        content::text_reveal::ShapedGlyphLine("Hello", 72.0f, bad_spec, 4.0f, 0.0f, engine),
        std::runtime_error
    );
}
