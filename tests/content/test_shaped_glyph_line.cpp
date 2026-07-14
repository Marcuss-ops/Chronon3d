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

TEST_CASE("ShapedGlyphLine: width matches legacy measure_text_width") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line = content::text_reveal::ShapedGlyphLine::try_shape("Hello", 72.0f, spec, 4.0f, 0.0f, engine).value();

    f32 expected = content::text_reveal::measure_text_width("Hello", 72.0f, spec, 4.0f, engine);
    CHECK(line.width() == expected);
    CHECK(line.width() > 0.0f);
}

TEST_CASE("ShapedGlyphLine: layout matches legacy layout_glyphs") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    content::text_reveal::ShapedGlyphLine line = content::text_reveal::ShapedGlyphLine::try_shape("Hello", 72.0f, spec, 4.0f, 0.0f, engine).value();

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
    content::text_reveal::ShapedGlyphLine line = content::text_reveal::ShapedGlyphLine::try_shape("ABC", 72.0f, spec, 4.0f, 10.0f, engine).value();

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
    content::text_reveal::ShapedGlyphLine line = content::text_reveal::ShapedGlyphLine::try_shape("Hello", 72.0f, spec, 4.0f, 0.0f, engine).value();

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
    content::text_reveal::ShapedGlyphLine line = content::text_reveal::ShapedGlyphLine::try_shape("Hello", 72.0f, spec, 4.0f, 0.0f, engine).value();

    CHECK(line.reveal_count(-0.5f) == 0);
    CHECK(line.reveal_count(0.0f) == 0);
    CHECK(line.reveal_count(1.0f) == line.layout().size());
    CHECK(line.reveal_count(2.0f) == line.layout().size());

    size_t mid = line.reveal_count(0.5f);
    CHECK(mid > 0);
    CHECK(mid <= line.layout().size());
}

TEST_CASE("ShapedGlyphLine: layout_glyphs throws std::runtime_error on non-existent font path (fail-loud contract)") {
    // TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL — the historical 6-arg
    // fail-loud ctor (`std::runtime_error` on bad spec) was [[deprecated]]
    // during this chore. The fail-loud contract is preserved via the
    // canonical free function `layout_glyphs(...)` which throws
    // `std::runtime_error(make_shape_error_message(...))` when shaping fails.
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec bad_spec{"assets/fonts/NonExistentFont.ttf", "Unknown", 400};

    CHECK_THROWS_AS(
        content::text_reveal::layout_glyphs("Hello", 72.0f, bad_spec, 4.0f, 0.0f, engine),
        std::runtime_error
    );
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

    // Construct ShapedGlyphLine via canonical factory (Point 8 fail-soft).
    // TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL: migrated off the [[deprecated]]
    // 6-arg ctor onto `try_shape(...).value()` — same single-shape call + fail-loud
    // semantic, no deprecation warning.
    content::text_reveal::ShapedGlyphLine line = content::text_reveal::ShapedGlyphLine::try_shape(text_200, 72.0f, spec, 4.0f, 0.0f, engine).value();

    // After construction, counter must be exactly 1 (single engine.shape_text call).
    CHECK(content::text_reveal::test_support::get_shape_call_count() == 1);

    // Accessor invocations must NOT trigger additional shaping calls
    // (Point 8 single-shape efficiency contract holds across all accessors).
    const f32 w        = line.width();
    auto         glyphs = line.layout();
    auto         box    = line.bbox();
    const f32   cur_end = line.cursor_at_end();
    const auto   count  = line.reveal_count(0.5f);

    // Counter stays at 1 — accessors read from m_run, not re-shape.
    CHECK(content::text_reveal::test_support::get_shape_call_count() == 1);

    // Sanity: the accessor outputs are non-trivial (defensive guard against
    // a future refactor that accidentally returns zeros without shaping).
    CHECK(w > 0.0f);
    CHECK(glyphs.size() > 0u);
    CHECK(cur_end > 0.0f);
    CHECK(count > 0u);
}
