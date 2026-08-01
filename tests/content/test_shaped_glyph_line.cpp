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
#include "content/common/text/glyph_layout_test_support.hpp"

#include <stdexcept>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

TEST_CASE("ShapedGlyphLine: shape_glyph_line width is canonical") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    auto shaped = content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, spec, 4.0f, /*ref_offset_x=*/0.0f, engine);
    REQUIRE(shaped.has_value());

    REQUIRE(shaped->width() > 0.0f);
}

TEST_CASE("ShapedGlyphLine: shape_glyph_line layout is canonical") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    auto shaped = content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, spec, 4.0f, /*ref_offset_x=*/0.0f, engine);
    REQUIRE(shaped.has_value());

    const auto glyphs = shaped->layout();
    REQUIRE(!glyphs.empty());
    REQUIRE(glyphs.front().width > 0.0f);
}

TEST_CASE("ShapedGlyphLine: cursor positions are monotonic and span the line") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    auto shaped = content::text_reveal::shape_glyph_line(
        "ABC", 72.0f, spec, 4.0f, /*ref_offset_x=*/10.0f, engine);
    REQUIRE(shaped.has_value());

    CHECK(shaped->cursor_position(0) == 10.0f);
    CHECK(shaped->cursor_position(0) < shaped->cursor_position(1));
    CHECK(shaped->cursor_position(1) < shaped->cursor_position(2));
    CHECK(shaped->cursor_position(2) < shaped->cursor_position(3));
    CHECK(shaped->cursor_at_end() == shaped->cursor_position(3));
}

TEST_CASE("ShapedGlyphLine: bbox has non-negative width and height") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    auto shaped = content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, spec, 4.0f, /*ref_offset_x=*/0.0f, engine);
    REQUIRE(shaped.has_value());

    auto box = shaped->bbox();
    CHECK(box.width() >= 0.0f);
    CHECK(box.height() >= 0.0f);
    CHECK(box.x1 >= box.x0);
    CHECK(box.y1 >= box.y0);
}

TEST_CASE("ShapedGlyphLine: reveal_count is clamped to [0, glyph_count]") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    auto shaped = content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, spec, 4.0f, /*ref_offset_x=*/0.0f, engine);
    REQUIRE(shaped.has_value());
    const auto glyph_count = shaped->layout().size();

    CHECK(shaped->reveal_count(-0.5f) == 0);
    CHECK(shaped->reveal_count(0.0f) == 0);
    CHECK(shaped->reveal_count(1.0f) == glyph_count);
    CHECK(shaped->reveal_count(2.0f) == glyph_count);

    size_t mid = shaped->reveal_count(0.5f);
    CHECK(mid > 0);
    CHECK(mid <= glyph_count);
}

TEST_CASE("shape_glyph_line: canonical offset is retained by all layout accessors") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    constexpr f32 kOffset = 37.5f;
    content::text_reveal::test_support::reset_shape_call_counter();
    auto shaped = content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, spec, 4.0f, kOffset, engine);
    CHECK(content::text_reveal::test_support::get_shape_call_count() == 1);
    REQUIRE(shaped.has_value());

    const auto positions = shaped->layout();
    REQUIRE(!positions.empty());
    CHECK(shaped->cursor_position(0) == kOffset);
    CHECK(positions.front().center_x > kOffset);
    CHECK(shaped->cursor_at_end() > kOffset);

    // Every accessor above reads the same cached shape.
    CHECK(content::text_reveal::test_support::get_shape_call_count() == 1);
}

TEST_CASE("shape_glyph_line returns no line for a non-existent font path") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec bad_spec{"assets/fonts/NonExistentFont.ttf", "Unknown", 400};

    const auto shaped = content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, bad_spec, 4.0f, 0.0f, engine);
    CHECK_FALSE(shaped.has_value());
}

// ── Counter test (TICKET-FIX-TEXT-SHAPING-DEDUP-V1) ─────────────────────
//
// Verifies that constructing a ShapedGlyphLine with a 200-glyph text
// (B02 Typewriter200Glyphs equivalent) triggers EXACTLY ONE engine.shape_text
// call — the Point-8 single-shape efficiency contract.  All accessor
// methods (width/layout/bbox/cursor/reveal_count) MUST read from the
// cached m_run; re-shaping on accessor invocation would re-introduce
// the lag the F6.2 fix removed.
//
// If this test starts failing with counter > 1, it means a future
// refactor re-introduced the redundant HarfBuzz bevel — re-open the
// ticket and fix the new code path.
TEST_CASE("ShapedGlyphLine: shape_calls_per_line counter == 1 on B02-equivalent 200-glyph line") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};

    // B02 equivalent: 200 glyphs of repeating Latin text.
    // Same shape complexity as bench_corpus_scenes.cpp::bench_b02_typewriter_200_glyphs().
    std::string text_200;
    text_200.reserve(200);
    const std::string pangram_loop =
        "THEQUICKBROWNFOXJUMPSOVERTHELAZYDOG";      // 35 chars
    while (text_200.size() < 200) {
        text_200 += pangram_loop;
    }
    text_200.resize(200);

    // Reset before each measurement (counter is global).
    content::text_reveal::test_support::reset_shape_call_counter();
    REQUIRE(content::text_reveal::test_support::get_shape_call_count() == 0);

    // Construct the canonical shape (fail-soft).
    auto shaped = content::text_reveal::shape_glyph_line(
        text_200, 72.0f, spec, 4.0f, /*ref_offset_x=*/0.0f, engine);
    REQUIRE(shaped.has_value());

    // After construction, counter must be exactly 1 (single engine.shape_text call).
    CHECK(content::text_reveal::test_support::get_shape_call_count() == 1);

    // Accessor invocations must NOT trigger additional shaping calls
    // (Point 8 single-shape efficiency contract holds across all accessors).
    const f32 w        = shaped->width();
    auto         glyphs = shaped->layout();
    auto         box    = shaped->bbox();
    const f32   cur_end = shaped->cursor_at_end();
    const auto   count  = shaped->reveal_count(0.5f);

    // Counter stays at 1 — accessors read from m_run, not re-shape.
    CHECK(content::text_reveal::test_support::get_shape_call_count() == 1);

    // Sanity: the accessor outputs are non-trivial (defensive guard against
    // a future refactor that accidentally returns zeros without shaping).
    CHECK(w > 0.0f);
    CHECK(glyphs.size() > 0u);
    CHECK(cur_end > 0.0f);
    CHECK(count > 0u);
}
