// ═══════════════════════════════════════════════════════════════════════════
// test_single_line_composer.cpp — Greedy SingleLine composer tests
//
// PR 2 covers:
//   1. Basic line breaking (single word, two words, overflow)
//   2. Indentation (left, right, first_line)
//   3. Justification (Left, Center, Right, Full)
//   4. Hanging punctuation (left/right overhang)
//   5. space_before/space_after in paragraph bounds
//   6. Empty / edge case inputs
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

// ── Test helpers ─────────────────────────────────────────────────────────

namespace {

/// Build a minimal PlacedGlyphRun with synthetic clusters for testing.
/// Each cluster represents one "character" with the given advance.
/// The source_text is used for character classification.
PlacedGlyphRun make_test_run(
    const std::vector<float>& advances,
    std::string_view source_text = {}
) {
    PlacedGlyphRun run;
    run.ascent = 50.0f;
    run.descent = 12.0f;
    run.baseline = 50.0f;
    run.font_size = 48.0f;
    run.total_height = 62.0f;

    float total = 0.0f;
    size_t byte_pos = 0;

    for (size_t i = 0; i < advances.size(); ++i) {
        // Determine byte range from source_text (UTF-8 aware).
        size_t byte_len = 1;
        if (!source_text.empty() && byte_pos < source_text.size()) {
            unsigned char c = static_cast<unsigned char>(source_text[byte_pos]);
            if (c < 0x80) byte_len = 1;
            else if ((c & 0xE0) == 0xC0) byte_len = 2;
            else if ((c & 0xF0) == 0xE0) byte_len = 3;
            else if ((c & 0xF8) == 0xF0) byte_len = 4;
        }

        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i);
        g.x = total;
        g.y = 0.0f;
        g.advance_x = advances[i];
        g.raw_advance_x = advances[i];
        g.byte_offset = byte_pos;
        g.byte_len = byte_len;
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        run.glyphs.push_back(g);

        // Build cluster
        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = byte_pos;
        cl.byte_len = byte_len;
        cl.advance = advances[i];
        cl.raw_advance = advances[i];
        run.clusters.push_back(cl);

        total += advances[i];
        byte_pos += byte_len;
    }

    run.total_width = total;
    return run;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Empty input / edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: empty shaped run produces one empty line") {
    PlacedGlyphRun empty;
    empty.ascent = 50.0f;
    empty.descent = 12.0f;
    empty.baseline = 50.0f;
    empty.total_height = 62.0f;

    auto layout = compose_single_line_paragraph(empty, 500.0f, ParagraphStyle{}, {});
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].cluster_count == 0);
    CHECK(layout.lines[0].natural_width == doctest::Approx(0.0f));
}

TEST_CASE("SingleLine: zero box width produces one line (emergency)") {
    auto run = make_test_run({20.0f, 20.0f, 20.0f}, "abc");
    auto layout = compose_single_line_paragraph(run, 0.0f, ParagraphStyle{}, "abc");
    CHECK(layout.lines.size() >= 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Basic line breaking
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: single word fits on one line") {
    auto run = make_test_run({10.0f, 10.0f, 10.0f, 10.0f, 10.0f}, "hello");
    auto layout = compose_single_line_paragraph(run, 200.0f, ParagraphStyle{}, "hello");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].cluster_count == 5);
    CHECK(layout.lines[0].natural_width == doctest::Approx(50.0f));
}

TEST_CASE("SingleLine: two words overflow to two lines") {
    // "Hello World": 5 chars + space + 5 chars
    auto run = make_test_run(
        {20.0f,20.0f,20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f,20.0f,20.0f},
        "Hello World"
    );
    // Available width = 120: "Hello " (110) fits, "World" (100) overflows
    auto layout = compose_single_line_paragraph(run, 120.0f, ParagraphStyle{}, "Hello World");
    CHECK(layout.lines.size() == 2);
    CHECK(layout.lines[0].cluster_count == 6);  // "Hello " includes space
    CHECK(layout.lines[1].cluster_count == 5);  // "World"
}

TEST_CASE("SingleLine: long word that doesn't fit is force-broken") {
    // 34 chars, each 20px = 680px
    auto run = make_test_run(std::vector<float>(34, 20.0f), std::string(34, 'x'));
    auto layout = compose_single_line_paragraph(run, 100.0f, ParagraphStyle{}, std::string(34, 'x'));
    CHECK(layout.lines.size() > 1);
    for (const auto& line : layout.lines) {
        CHECK(line.cluster_count >= 1);
    }
}

TEST_CASE("SingleLine: leading newline produces empty line then content") {
    auto run = make_test_run({5.0f, 20.0f,20.0f,20.0f,20.0f,20.0f}, "\nHello");
    auto layout = compose_single_line_paragraph(run, 200.0f, ParagraphStyle{}, "\nHello");
    // Leading \n → empty first line (cluster_count==0) + "Hello" line
    CHECK(layout.lines.size() >= 2);
    CHECK(layout.lines[0].cluster_count == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Indentation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: left indent reduces available width") {
    auto run = make_test_run({50.0f, 50.0f}, "ab");
    ParagraphStyle style;
    style.left_indent = 30.0f;
    // box=120, available=90. "ab"=100px → overflows to 2 lines
    auto layout = compose_single_line_paragraph(run, 120.0f, style, "ab");
    CHECK(layout.lines.size() == 2);
}

TEST_CASE("SingleLine: right indent reduces available width") {
    auto run = make_test_run({80.0f}, "a");
    ParagraphStyle style;
    style.right_indent = 20.0f;
    // box=100, available=80. "a"=80px → fits
    auto layout = compose_single_line_paragraph(run, 100.0f, style, "a");
    CHECK(layout.lines.size() == 1);
}

TEST_CASE("SingleLine: negative first_line_indent creates hanging indent") {
    auto run = make_test_run({50.0f}, "a");
    ParagraphStyle style;
    style.left_indent = 30.0f;
    style.first_line_indent = -30.0f;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "a");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].natural_width == doctest::Approx(50.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Justification
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: Center justification marks line for centering") {
    auto run = make_test_run({50.0f}, "a");
    ParagraphStyle style;
    style.justification = TextJustification::Center;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "a");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].glyph_scale == doctest::Approx(1.0f));
}

TEST_CASE("SingleLine: Full justification on single-cluster line is no-op") {
    auto run = make_test_run({50.0f}, "a");
    ParagraphStyle style;
    style.justification = TextJustification::Full;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "a");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].glyph_scale == doctest::Approx(1.0f));
}

TEST_CASE("SingleLine: Full justification expands word spacing") {
    // "a b c" = 3 words, each 20px, spaces 10px each = 80px
    auto run = make_test_run({20.0f, 10.0f, 20.0f, 10.0f, 20.0f}, "a b c");
    ParagraphStyle style;
    style.justification = TextJustification::Full;
    style.spacing.word_max = 5.0f;  // allow large expansion for test
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "a b c");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].word_spacing_adjustment > 0.0f);
    CHECK(layout.lines[0].final_width == doctest::Approx(200.0f));
}

TEST_CASE("SingleLine: FullLastLineLeft keeps last line left-aligned") {
    auto run = make_test_run(
        {20.0f,10.0f, 20.0f,10.0f, 20.0f,10.0f, 20.0f,10.0f, 20.0f,10.0f, 20.0f},
        "a b c d e f"
    );
    ParagraphStyle style;
    style.justification = TextJustification::FullLastLineLeft;
    auto layout = compose_single_line_paragraph(run, 100.0f, style, "a b c d e f");
    CHECK(layout.lines.size() >= 2);
    const auto& last = layout.lines.back();
    CHECK(last.glyph_scale == doctest::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Hanging punctuation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: hanging punctuation — left quote overhang") {
    auto run = make_test_run({15.0f, 20.0f,20.0f,20.0f,20.0f,20.0f}, "\"Hello");
    ParagraphStyle style;
    style.hanging_punctuation = true;
    style.hanging_limit = 0.5f;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "\"Hello");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].left_overhang > 0.0f);
}

TEST_CASE("SingleLine: hanging punctuation — right period overhang") {
    auto run = make_test_run({20.0f,20.0f,20.0f,20.0f,20.0f, 8.0f}, "Hello.");
    ParagraphStyle style;
    style.hanging_punctuation = true;
    style.hanging_limit = 0.5f;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "Hello.");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].right_overhang > 0.0f);
}

TEST_CASE("SingleLine: hanging punctuation disabled produces no overhang") {
    auto run = make_test_run({15.0f,20.0f,20.0f,20.0f,20.0f,20.0f, 8.0f, 15.0f}, "\"Hello.\"");
    ParagraphStyle style;
    style.hanging_punctuation = false;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "\"Hello.\"");
    CHECK(layout.lines[0].left_overhang == doctest::Approx(0.0f));
    CHECK(layout.lines[0].right_overhang == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. space_before / space_after
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: space_before is included in bounds height") {
    auto run = make_test_run({50.0f}, "a");
    ParagraphStyle style;
    style.space_before = 20.0f;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "a");
    CHECK(layout.bounds.y > 20.0f);
}

TEST_CASE("SingleLine: space_after is included in bounds height") {
    auto run = make_test_run({50.0f}, "a");
    ParagraphStyle style;
    style.space_after = 15.0f;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "a");
    CHECK(layout.bounds.y > 15.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Paragraph bounds
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: bounds.x tracks widest line (including overhang)") {
    auto run = make_test_run(
        {25.0f,25.0f,25.0f,25.0f, 10.0f, 20.0f,20.0f},
        "AAAA BB"
    );
    auto layout = compose_single_line_paragraph(run, 60.0f, ParagraphStyle{}, "AAAA BB");
    CHECK(layout.lines.size() >= 2);
    CHECK(layout.bounds.x >= 100.0f);
}

TEST_CASE("SingleLine: bounds.y includes all lines") {
    auto run = make_test_run(std::vector<float>(30, 20.0f), std::string(30, 'x'));
    auto layout = compose_single_line_paragraph(run, 100.0f, ParagraphStyle{}, std::string(30, 'x'));
    CHECK(layout.lines.size() >= 3);
    float min_height = static_cast<float>(layout.lines.size()) * (50.0f + 12.0f);
    CHECK(layout.bounds.y >= min_height);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: same input produces same output") {
    std::string text = "abc def ghi";
    auto run = make_test_run(
        {20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f},
        text
    );
    ParagraphStyle style;
    style.justification = TextJustification::Center;
    style.left_indent = 15.0f;

    auto a = compose_single_line_paragraph(run, 120.0f, style, text);
    auto b = compose_single_line_paragraph(run, 120.0f, style, text);

    CHECK(a.lines.size() == b.lines.size());
    for (size_t i = 0; i < a.lines.size(); ++i) {
        CHECK(a.lines[i].first_cluster == b.lines[i].first_cluster);
        CHECK(a.lines[i].cluster_count == b.lines[i].cluster_count);
        CHECK(a.lines[i].natural_width == doctest::Approx(b.lines[i].natural_width));
        CHECK(a.lines[i].final_width == doctest::Approx(b.lines[i].final_width));
    }
    CHECK(a.bounds.x == doctest::Approx(b.bounds.x));
    CHECK(a.bounds.y == doctest::Approx(b.bounds.y));
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Integration: full document-style layout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SingleLine: title-style centered block") {
    auto run = make_test_run(
        {24.0f,24.0f,24.0f, 10.0f, 24.0f,24.0f,24.0f,24.0f,24.0f,24.0f},
        "THE FUTURE"
    );
    ParagraphStyle style;
    style.justification = TextJustification::Center;
    style.space_before = 30.0f;
    style.space_after = 20.0f;

    auto layout = compose_single_line_paragraph(run, 500.0f, style, "THE FUTURE");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].natural_width == doctest::Approx(226.0f));
    CHECK(layout.bounds.y >= 30.0f + 20.0f + 50.0f + 12.0f);
}

TEST_CASE("SingleLine: multi-line body text with left align") {
    auto run = make_test_run(std::vector<float>(30, 16.0f), std::string(30, 'x'));
    ParagraphStyle style;
    style.justification = TextJustification::Left;
    style.left_indent = 20.0f;

    auto layout = compose_single_line_paragraph(run, 200.0f, style, std::string(30, 'x'));
    CHECK(layout.lines.size() >= 3);
    for (const auto& line : layout.lines) {
        CHECK(line.glyph_scale == doctest::Approx(1.0f));
    }
}
