// ═══════════════════════════════════════════════════════════════════════════
// test_text_document.cpp — TextDocument, TextStyleSpan, ParagraphRange tests
//
// PR 1 covers:
//   1. TextDocument construction + validation
//   2. TextStyleSpan ordering / overlap detection
//   3. ParagraphRange coverage requirements
//   4. split_paragraphs() on hard breaks (\n, \n\n, U+2029)
//   5. Hash determinism (same input → same hash)
//   6. Hash collision avoidance (different input → different hash)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

TextSpec make_defaults() {
    TextSpec ts;
    ts.content.value = "";  // placeholder — actual text is in TextDocument.utf8
    ts.font.font_path = "assets/fonts/Inter-Bold.ttf";
    ts.font.font_family = "Inter";
    ts.font.font_weight = 700;
    ts.font.font_size = 48.0f;
    ts.appearance.color = {1.0f, 1.0f, 1.0f, 1.0f};
    ts.layout.box = {1920.0f, 1080.0f};
    ts.layout.align = TextAlign::Center;
    ts.layout.vertical_align = VerticalAlign::Middle;
    ts.placement = TextPlacement{
        TextPlacementKind::Absolute, {0.0f, 0.0f}};
    return ts;
}

TextStyleSpan make_span(size_t start, size_t end, f32 font_size = 64.0f) {
    TextStyleSpan s;
    s.byte_start = start;
    s.byte_end = end;
    s.font = FontSpec{
        .font_path = "assets/fonts/Poppins-Bold.ttf",
        .font_family = "Poppins",
        .font_weight = 800,
        .font_size = font_size,
    };
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Validation — empty document
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: empty document is valid") {
    TextDocument doc;
    doc.defaults = make_defaults();
    CHECK(doc.validate());
}

TEST_CASE("TextDocument: empty utf8 with single empty paragraph validates") {
    TextDocument doc;
    doc.defaults = make_defaults();
    doc.paragraphs.push_back(ParagraphRange{0, 0, {}});
    CHECK(doc.validate());
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Validation — spans
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: sorted non-overlapping spans are valid") {
    TextDocument doc;
    doc.utf8 = "Hello World";
    doc.defaults = make_defaults();
    doc.spans.push_back(make_span(0, 5, 72.0f));
    doc.spans.push_back(make_span(6, 11, 36.0f));
    CHECK(doc.validate());
}

TEST_CASE("TextDocument: overlapping spans fail validation") {
    TextDocument doc;
    doc.utf8 = "Hello";
    doc.defaults = make_defaults();
    doc.spans.push_back(make_span(0, 3));
    doc.spans.push_back(make_span(2, 5));  // overlap at byte 2
    CHECK_FALSE(doc.validate());
}

TEST_CASE("TextDocument: unsorted spans fail validation") {
    TextDocument doc;
    doc.utf8 = "Hello";
    doc.defaults = make_defaults();
    doc.spans.push_back(make_span(3, 5));
    doc.spans.push_back(make_span(0, 2));  // out of order
    CHECK_FALSE(doc.validate());
}

TEST_CASE("TextDocument: span out of bounds fails validation") {
    TextDocument doc;
    doc.utf8 = "Hi";  // 2 bytes
    doc.defaults = make_defaults();
    doc.spans.push_back(make_span(0, 5));  // byte_end > utf8.size()
    CHECK_FALSE(doc.validate());
}

TEST_CASE("TextDocument: empty span (start == end) fails validation") {
    TextDocument doc;
    doc.utf8 = "Hi";
    doc.defaults = make_defaults();
    doc.spans.push_back(TextStyleSpan{1, 1});  // zero-width span
    CHECK_FALSE(doc.validate());
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Validation — paragraphs
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: paragraphs covering entire document are valid") {
    TextDocument doc;
    doc.utf8 = "Hello";
    doc.defaults = make_defaults();
    doc.paragraphs.push_back(ParagraphRange{0, 5, {}});
    CHECK(doc.validate());
}

TEST_CASE("TextDocument: paragraphs not covering end fails validation") {
    TextDocument doc;
    doc.utf8 = "Hello World";  // 11 bytes
    doc.defaults = make_defaults();
    doc.paragraphs.push_back(ParagraphRange{0, 5, {}});  // only covers "Hello"
    CHECK_FALSE(doc.validate());
}

TEST_CASE("TextDocument: overlapping paragraphs fail validation") {
    TextDocument doc;
    doc.utf8 = "Hello World";
    doc.defaults = make_defaults();
    doc.paragraphs.push_back(ParagraphRange{0, 6, {}});
    doc.paragraphs.push_back(ParagraphRange{4, 11, {}});  // overlap
    CHECK_FALSE(doc.validate());
}

TEST_CASE("TextDocument: empty paragraphs vector passes validation (split_paragraphs will fill it)") {
    TextDocument doc;
    doc.utf8 = "Hello";
    doc.defaults = make_defaults();
    // paragraphs is empty — validate() returns true (lazy population).
    CHECK(doc.validate());
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. UTF-8 validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: valid UTF-8 passes") {
    TextDocument doc;
    doc.utf8 = "Hello — World";  // em-dash is valid 3-byte UTF-8
    doc.defaults = make_defaults();
    CHECK(doc.validate());
}

TEST_CASE("TextDocument: truncated UTF-8 fails") {
    TextDocument doc;
    doc.utf8 = "Hello \xC0";  // 0xC0 alone is invalid (not a valid leading byte)
    doc.defaults = make_defaults();
    CHECK_FALSE(doc.validate());
}

TEST_CASE("TextDocument: overlong encoding fails") {
    // 0xC0 0xAF is an overlong encoding of '/' (U+002F)
    TextDocument doc;
    doc.utf8 = std::string("AB\xC0\xAF");
    doc.defaults = make_defaults();
    CHECK_FALSE(doc.validate());
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. split_paragraphs — basic cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: split_paragraphs on single line produces 1 paragraph") {
    TextDocument doc;
    doc.utf8 = "Hello World";
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    CHECK(count == 1);
    REQUIRE(doc.paragraphs.size() == 1);
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[0].byte_end == doc.utf8.size());
}

TEST_CASE("TextDocument: split_paragraphs on two lines produces 2 paragraphs") {
    TextDocument doc;
    doc.utf8 = "Line 1\nLine 2";
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    CHECK(count == 2);
    REQUIRE(doc.paragraphs.size() == 2);
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[1].byte_start > doc.paragraphs[0].byte_end);
}

TEST_CASE("TextDocument: split_paragraphs collapses \\n\\n into one break") {
    TextDocument doc;
    doc.utf8 = "A\n\nB";
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    CHECK(count == 2);
    REQUIRE(doc.paragraphs.size() == 2);
}

TEST_CASE("TextDocument: split_paragraphs handles trailing newline") {
    TextDocument doc;
    doc.utf8 = "Hello\n";
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    // "Hello" + trailing empty paragraph
    CHECK(count >= 1);
    REQUIRE_FALSE(doc.paragraphs.empty());
    CHECK(doc.paragraphs[0].byte_start == 0);
}

TEST_CASE("TextDocument: split_paragraphs on U+2029 separator") {
    // "Hello" + U+2029 + "World" → 2 paragraphs
    TextDocument doc;
    doc.utf8 = "Hello\xE2\x80\xA9World";
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    CHECK(count == 2);
    REQUIRE(doc.paragraphs.size() == 2);
    // First paragraph covers "Hello" (5 bytes)
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[0].byte_end == 5);
    // Second paragraph starts after U+2029 (3 bytes) = byte 8
    CHECK(doc.paragraphs[1].byte_start == 8);
    CHECK(doc.paragraphs[1].byte_end == doc.utf8.size());
}

TEST_CASE("TextDocument: sole newline produces predictable paragraphs") {
    // A document of just "\n" represents a boundary between two empty
    // paragraphs.  The composer will need to handle zero-width paragraphs
    // gracefully.
    TextDocument doc;
    doc.utf8 = "\n";
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    CHECK(count == 2);
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[0].byte_end == 0);
    CHECK(doc.paragraphs[1].byte_start == 1);
    CHECK(doc.paragraphs[1].byte_end == 1);
}

TEST_CASE("TextDocument: split_paragraphs on empty string produces 1 empty paragraph") {
    TextDocument doc;
    doc.defaults = make_defaults();

    size_t count = doc.split_paragraphs();
    CHECK(count == 1);
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[0].byte_end == 0);
}

TEST_CASE("TextDocument: split_paragraphs preserves existing valid paragraphs") {
    TextDocument doc;
    doc.utf8 = "Hello World";
    doc.defaults = make_defaults();
    doc.paragraphs.push_back(ParagraphRange{0, 11, {}});

    size_t count = doc.split_paragraphs();
    CHECK(count == 1);  // unchanged
    CHECK(doc.paragraphs[0].byte_start == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Hashing — determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: hash is deterministic (same input → same hash)") {
    TextDocument doc;
    doc.utf8 = "Hello World";
    doc.defaults = make_defaults();

    ParagraphStyle ps;
    ps.justification = TextJustification::Center;
    ps.left_indent = 20.0f;
    doc.split_paragraphs(ps);

    TextStyleSpan span = make_span(0, 5, 72.0f);
    doc.spans.push_back(span);

    u64 h1 = hash_text_document(doc);
    u64 h2 = hash_text_document(doc);
    CHECK(h1 == h2);
}

TEST_CASE("TextDocument: hash changes when utf8 changes") {
    TextDocument a;
    a.utf8 = "Hello";
    a.defaults = make_defaults();

    TextDocument b = a;
    b.utf8 = "World";

    CHECK(hash_text_document(a) != hash_text_document(b));
}

TEST_CASE("TextDocument: hash changes when span is added") {
    TextDocument a;
    a.utf8 = "Hello World";
    a.defaults = make_defaults();

    TextDocument b = a;
    b.spans.push_back(make_span(0, 5, 72.0f));

    CHECK(hash_text_document(a) != hash_text_document(b));
}

TEST_CASE("TextDocument: hash changes when paragraph style differs") {
    TextDocument a;
    a.utf8 = "Hello";
    a.defaults = make_defaults();
    a.split_paragraphs(ParagraphStyle{});

    TextDocument b = a;
    ParagraphStyle ps;
    ps.justification = TextJustification::Full;
    b.split_paragraphs(ps);

    CHECK(hash_text_document(a) != hash_text_document(b));
}

TEST_CASE("TextDocument: hash changes when defaults.font_size changes") {
    TextDocument a;
    a.utf8 = "Hello";
    a.defaults = make_defaults();

    TextDocument b = a;
    b.defaults.font.font_size = 96.0f;

    CHECK(hash_text_document(a) != hash_text_document(b));
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. TextStyleSpan — field-level hashing
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextStyleSpan: hash is deterministic") {
    TextStyleSpan s;
    s.byte_start = 0;
    s.byte_end = 10;
    s.font = FontSpec{.font_family = "Inter", .font_weight = 700};
    s.tracking = 2.0f;

    CHECK(hash_text_style_span(s) == hash_text_style_span(s));
}

TEST_CASE("TextStyleSpan: different tracking produces different hash") {
    TextStyleSpan a;
    a.byte_start = 0;
    a.byte_end = 10;
    a.tracking = 0.0f;

    TextStyleSpan b = a;
    b.tracking = 5.0f;

    CHECK(hash_text_style_span(a) != hash_text_style_span(b));
}

TEST_CASE("TextStyleSpan: different byte range produces different hash") {
    TextStyleSpan a;
    a.byte_start = 0;
    a.byte_end = 5;

    TextStyleSpan b = a;
    b.byte_start = 3;

    CHECK(hash_text_style_span(a) != hash_text_style_span(b));
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. ParagraphStyle — hashing
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ParagraphStyle: hash is deterministic") {
    ParagraphStyle ps;
    ps.justification = TextJustification::Full;
    ps.left_indent = 40.0f;
    ps.hanging_punctuation = true;

    CHECK(hash_paragraph_style(ps) == hash_paragraph_style(ps));
}

TEST_CASE("ParagraphStyle: different composer produces different hash") {
    ParagraphStyle a;
    a.composer = ParagraphComposer::SingleLine;

    ParagraphStyle b = a;
    b.composer = ParagraphComposer::EveryLine;

    CHECK(hash_paragraph_style(a) != hash_paragraph_style(b));
}

TEST_CASE("ParagraphStyle: different indentation produces different hash") {
    ParagraphStyle a;
    a.left_indent = 0.0f;

    ParagraphStyle b = a;
    b.left_indent = 30.0f;

    CHECK(hash_paragraph_style(a) != hash_paragraph_style(b));
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. ParagraphStyle — defaults
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ParagraphStyle: default is SingleLine, Left justified") {
    ParagraphStyle ps;
    CHECK(ps.composer == ParagraphComposer::SingleLine);
    CHECK(ps.justification == TextJustification::Left);
    CHECK(ps.left_indent == doctest::Approx(0.0f));
    CHECK(ps.right_indent == doctest::Approx(0.0f));
    CHECK(ps.space_before == doctest::Approx(0.0f));
    CHECK(ps.space_after == doctest::Approx(0.0f));
    CHECK_FALSE(ps.hanging_punctuation);
    CHECK_FALSE(ps.hyphenation);
}

TEST_CASE("ParagraphStyle: spacing defaults are centered on natural") {
    ParagraphSpacing sp;
    CHECK(sp.word_desired == doctest::Approx(1.0f));
    CHECK(sp.word_min < sp.word_desired);
    CHECK(sp.word_max > sp.word_desired);
    CHECK(sp.glyph_scale_desired == doctest::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. Integration — document with spans and paragraphs
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDocument: full document with spans + paragraphs validates") {
    TextDocument doc;
    doc.utf8 = "Hello World\nThis is a second line.";
    doc.defaults = make_defaults();

    // Add a span for the first word.
    doc.spans.push_back(make_span(0, 5, 72.0f));

    // Split into paragraphs.
    ParagraphStyle title_style;
    title_style.justification = TextJustification::Center;

    ParagraphStyle body_style;
    body_style.justification = TextJustification::Left;
    body_style.left_indent = 20.0f;

    doc.split_paragraphs(title_style);
    // Override second paragraph's style.
    if (doc.paragraphs.size() >= 2) {
        doc.paragraphs[1].style = body_style;
    }

    CHECK(doc.validate());
    CHECK(doc.paragraphs.size() >= 2);
}
