// ═══════════════════════════════════════════════════════════════════════════
// test_unicode_line_breaking.cpp — UAX#14-style line breaking + CJK / Thai /
// Devanagari / Arabic / Hebrew / mixed-direction support.
//
// Uses synthetic PlacedGlyphRun inputs so the tests exercise the paragraph
// composer directly without requiring real fonts for every script.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

// Build a PlacedGlyphRun where each cluster has the same advance and one
// byte per "unit" (ASCII) or a caller-supplied byte length.  For multi-byte
// scripts the caller supplies both the source text and the advances.
PlacedGlyphRun make_test_run(
    const std::vector<float>& advances,
    std::string_view source_text
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
        g.byte_len = static_cast<u32>(byte_len);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = byte_pos;
        cl.byte_len = static_cast<u32>(byte_len);
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
// 1. CJK wrapping without spaces
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UnicodeLineBreak: CJK ideographs wrap without whitespace") {
    // 6 CJK characters, each 40px wide, box = 140px → should wrap into
    // at least 2 lines (3 chars + 3 chars) because breaks are allowed
    // between ideographs.
    const char* text = "\u4E2D\u6587\u4E0A\u6D77\u6D77\u4E0A";  // 中文上海海上
    auto run = make_test_run(std::vector<float>(6, 40.0f), text);

    ParagraphStyle style;
    style.composer = ParagraphComposer::SingleLine;
    auto layout = compose_single_line_paragraph(run, 140.0f, style, text);

    CHECK(layout.lines.size() >= 2);
    for (const auto& line : layout.lines) {
        CHECK(line.cluster_count >= 1);
    }
}

TEST_CASE("UnicodeLineBreak: CJK opening bracket prevents break after") {
    // 「開 (opening bracket + ideograph).  The opening bracket should not
    // end a line, so both characters must stay together even in a tiny box.
    const char* text = "\u300C\u958B";  // 「開
    auto run = make_test_run({40.0f, 40.0f}, text);

    ParagraphStyle style;
    style.kinsoku = true;  // kinsoku enforces no-break-after opening bracket
    auto layout = compose_single_line_paragraph(run, 50.0f, style, text);

    CHECK(layout.lines.size() == 1);
    CHECK(layout.lines[0].cluster_count == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Thai wrapping
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UnicodeLineBreak: Thai text wraps without whitespace") {
    // "สวัสดีประเทศไทย" (Hello Thailand) — no spaces.  With 14 chars of
    // 30px each in a 150px box, the composer should produce >1 line.
    const char* text = "\u0E2A\u0E27\u0E31\u0E2A\u0E14\u0E35\u0E1B\u0E23\u0E30\u0E40\u0E17\u0E28\u0E44\u0E17\u0E22";
    auto run = make_test_run(std::vector<float>(14, 30.0f), text);

    ParagraphStyle style;
    auto layout = compose_single_line_paragraph(run, 150.0f, style, text);

    CHECK(layout.lines.size() >= 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Devanagari wrapping
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UnicodeLineBreak: Devanagari text wraps without whitespace") {
    // "नमस्ते भारत" without space => "नमस्तेभारत".
    const char* text = "\u0928\u092E\u0938\u094D\u0924\u0947\u092D\u093E\u0930\u0924";
    auto run = make_test_run(std::vector<float>(10, 30.0f), text);

    ParagraphStyle style;
    auto layout = compose_single_line_paragraph(run, 120.0f, style, text);

    CHECK(layout.lines.size() >= 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Arabic / Hebrew — bidi runs are already split upstream, but the
//    composer still needs to break lines correctly for scripts without
//    spaces.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UnicodeLineBreak: Arabic shapes as a single run and wraps") {
    // "مرحبا" (Hello) repeated, no spaces.
    const char* text = "\u0645\u0631\u062D\u0628\u0627\u0645\u0631\u062D\u0628\u0627";
    auto run = make_test_run(std::vector<float>(10, 35.0f), text);

    ParagraphStyle style;
    auto layout = compose_single_line_paragraph(run, 140.0f, style, text);

    CHECK(layout.lines.size() >= 2);
}

TEST_CASE("UnicodeLineBreak: Hebrew shapes as a single run and wraps") {
    // "שלום" (Hello) repeated, no spaces.
    const char* text = "\u05E9\u05DC\u05D5\u05DD\u05E9\u05DC\u05D5\u05DD";
    auto run = make_test_run(std::vector<float>(8, 35.0f), text);

    ParagraphStyle style;
    auto layout = compose_single_line_paragraph(run, 140.0f, style, text);

    CHECK(layout.lines.size() >= 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Mixed LTR/RTL — the bidi segmenter produces separate runs upstream;
//    here we verify the composer can still break a run that logically
//    contains both scripts.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UnicodeLineBreak: mixed LTR + Arabic still breaks by width") {
    // "Helloمرحبا" — no spaces.
    const char* text = "Hello\u0645\u0631\u062D\u0628\u0627";
    std::vector<float> advances(9, 30.0f);
    auto run = make_test_run(advances, text);

    ParagraphStyle style;
    auto layout = compose_single_line_paragraph(run, 150.0f, style, text);

    CHECK(layout.lines.size() >= 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. max_lines + grapheme-safe ellipsis hint
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("UnicodeLineBreak: max_lines truncates and records ellipsis") {
    // 30 ASCII 'x' characters, 20px each, 100px box => 6 lines of 5 chars.
    std::string text(30, 'x');
    auto run = make_test_run(std::vector<float>(30, 20.0f), text);

    ParagraphStyle style;
    style.max_lines = 2;
    style.ellipsis = "...";
    auto layout = compose_single_line_paragraph(run, 100.0f, style, text);

    CHECK(layout.lines.size() == 2);
    CHECK(layout.truncated == true);
    CHECK(layout.rendered_ellipsis == "...");
}

TEST_CASE("UnicodeLineBreak: max_lines=0 does not truncate") {
    std::string text(30, 'x');
    auto run = make_test_run(std::vector<float>(30, 20.0f), text);

    ParagraphStyle style;
    style.max_lines = 0;
    auto layout = compose_single_line_paragraph(run, 100.0f, style, text);

    CHECK(layout.lines.size() > 2);
    CHECK(layout.truncated == false);
}

TEST_CASE("UnicodeLineBreak: truncation uses default U+2026 ellipsis") {
    std::string text(20, 'x');
    auto run = make_test_run(std::vector<float>(20, 20.0f), text);

    ParagraphStyle style;
    style.max_lines = 1;
    // ellipsis default is U+2026
    auto layout = compose_single_line_paragraph(run, 100.0f, style, text);

    CHECK(layout.lines.size() == 1);
    CHECK(layout.truncated == true);
    CHECK(layout.rendered_ellipsis == "\xE2\x80\xA6");
}
