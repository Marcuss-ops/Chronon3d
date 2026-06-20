// ═══════════════════════════════════════════════════════════════════════════
// test_every_line_composer.cpp — Knuth-Plass EveryLine composer tests
//
// PR 3 covers:
//   1. Basic DP: same input as greedy produces valid output
//   2. Optimality: DP avoids "lone word at end" that greedy produces
//   3. Badness: tight lines penalized, perfect fit gets 0
//   4. Edge cases: no breaks, empty input, single cluster
//   5. Determinism
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/every_line_composer.hpp>
#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

// ── Test helpers ─────────────────────────────────────────────────────────

namespace {

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

ParagraphLayout compose_every_line(
    const PlacedGlyphRun& shaped,
    float box_width,
    const ParagraphStyle& style,
    std::string_view source_text
) {
    ParagraphStyle s = style;
    s.composer = ParagraphComposer::EveryLine;
    return compose_single_line_paragraph(shaped, box_width, s, source_text);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Basic DP — same as greedy for simple cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("EveryLine: single word fits on one line") {
    auto run = make_test_run({10.0f,10.0f,10.0f,10.0f,10.0f}, "hello");
    auto layout = compose_every_line(run, 200.0f, ParagraphStyle{}, "hello");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].cluster_count == 5);
}

TEST_CASE("EveryLine: two words overflow to two lines") {
    auto run = make_test_run(
        {20.0f,20.0f,20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f,20.0f,20.0f},
        "Hello World"
    );
    auto layout = compose_every_line(run, 120.0f, ParagraphStyle{}, "Hello World");
    CHECK(layout.lines.size() == 2);
}

TEST_CASE("EveryLine: empty input produces empty layout") {
    PlacedGlyphRun empty;
    empty.ascent = 50.0f;
    empty.descent = 12.0f;
    auto layout = compose_every_line(empty, 500.0f, ParagraphStyle{}, {});
    CHECK(layout.lines.size() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Optimality — DP beats greedy on known patterns
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("EveryLine: avoids orphan word that greedy produces") {
    // "this is a very long title"
    // Advances: this=40, space=10, is=20, space=10, a=10, space=10,
    //           very=40, space=10, long=40, space=10, title=40
    // Total = 240px.  Box = 100px.
    //
    // Greedy (100px):
    //   "this is a" (40+10+20+10+10 = 90) fits
    //   "very long" (40+10+40 = 90) fits
    //   "title" (40) — orphan word alone!
    //
    // EveryLine DP (100px) should balance into 3 lines:
    //   "this is" (40+10+20 = 70)
    //   "a very" (10+10+40 = 60)
    //   "long title" (40+10+40 = 90)
    auto run = make_test_run(
        {40.0f, 10.0f, 20.0f, 10.0f, 10.0f, 10.0f, 40.0f, 10.0f, 40.0f, 10.0f, 40.0f},
        "this is a very long title"
    );
    ParagraphStyle style;
    style.composer = ParagraphComposer::EveryLine;

    auto layout = compose_single_line_paragraph(run, 100.0f, style, "this is a very long title");
    CHECK(layout.lines.size() == 3);
    // The last line should NOT be a single-word orphan.
    // DP gives "long title" (2 clusters) vs greedy's "title" (1 cluster).
    if (layout.lines.size() == 3) {
        CHECK(layout.lines.back().cluster_count >= 2);
    }
}

TEST_CASE("EveryLine: produces same result as greedy for simple single-line text") {
    auto run = make_test_run({10.0f,10.0f,10.0f}, "abc");
    ParagraphStyle greedy_style;
    greedy_style.composer = ParagraphComposer::SingleLine;
    auto greedy = compose_single_line_paragraph(run, 200.0f, greedy_style, "abc");

    auto every = compose_every_line(run, 200.0f, ParagraphStyle{}, "abc");

    CHECK(greedy.lines.size() == every.lines.size());
    CHECK(greedy.lines[0].cluster_count == every.lines[0].cluster_count);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Justification with EveryLine
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("EveryLine: Center justification") {
    auto run = make_test_run({50.0f}, "a");
    ParagraphStyle style;
    style.justification = TextJustification::Center;
    auto layout = compose_every_line(run, 200.0f, style, "a");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].glyph_scale == doctest::Approx(1.0f));
}

TEST_CASE("EveryLine: Full justification distributes slack") {
    auto run = make_test_run({20.0f, 10.0f, 20.0f, 10.0f, 20.0f}, "a b c");
    ParagraphStyle style;
    style.justification = TextJustification::Full;
    style.spacing.word_max = 5.0f;
    auto layout = compose_every_line(run, 200.0f, style, "a b c");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].final_width == doctest::Approx(200.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Indentation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("EveryLine: respects left indent") {
    auto run = make_test_run({50.0f, 50.0f}, "ab");
    ParagraphStyle style;
    style.left_indent = 30.0f;
    // box=120, available=90. "ab"=100 → 2 lines
    auto layout = compose_every_line(run, 120.0f, style, "ab");
    CHECK(layout.lines.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("EveryLine: same input produces same output") {
    std::string text = "abc def ghi jkl";
    auto run = make_test_run(
        {20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f, 10.0f, 20.0f,20.0f,20.0f},
        text
    );
    ParagraphStyle style;
    style.justification = TextJustification::Center;

    auto a = compose_every_line(run, 120.0f, style, text);
    auto b = compose_every_line(run, 120.0f, style, text);

    CHECK(a.lines.size() == b.lines.size());
    for (size_t i = 0; i < a.lines.size(); ++i) {
        CHECK(a.lines[i].first_cluster == b.lines[i].first_cluster);
        CHECK(a.lines[i].cluster_count == b.lines[i].cluster_count);
        CHECK(a.lines[i].natural_width == doctest::Approx(b.lines[i].natural_width));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Dispatch: SingleLine entry point with EveryLine composer mode
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("EveryLine: dispatch from compose_single_line_paragraph") {
    auto run = make_test_run({20.0f,20.0f,20.0f}, "abc");
    ParagraphStyle style;
    style.composer = ParagraphComposer::EveryLine;
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "abc");
    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].cluster_count == 3);
}

TEST_CASE("EveryLine: dispatch defaults to SingleLine when composer not set") {
    auto run = make_test_run({20.0f,20.0f,20.0f}, "abc");
    ParagraphStyle style;  // defaults to SingleLine
    auto layout = compose_single_line_paragraph(run, 200.0f, style, "abc");
    CHECK(layout.lines.size() == 1);
}
