#include <doctest/doctest.h>
#include <chronon3d/renderer/text_layout_engine.hpp>

using namespace chronon3d;

// Fixed-width char_width: every character is `size * 0.5` pixels wide.
static auto fixed_cw(float factor = 0.5f) {
    return [factor](char, float sz) -> float { return sz * factor; };
}

// ---------------------------------------------------------------------------
// No box: single line, no wrapping
// ---------------------------------------------------------------------------
TEST_CASE("TextLayout: no box returns single line") {
    TextLayoutInput in;
    in.text        = "Hello World";
    in.style.size  = 32.0f;
    in.char_width  = fixed_cw();

    auto r = TextLayoutEngine::layout(in);
    CHECK(r.lines.size() == 1);
    CHECK(r.lines[0].text == "Hello World");
    CHECK(r.resolved_font_size == doctest::Approx(32.0f));
}

// ---------------------------------------------------------------------------
// Box enabled: word wrap
// ---------------------------------------------------------------------------
TEST_CASE("TextLayout: wraps words to fit box width") {
    TextLayoutInput in;
    in.text        = "one two three four";
    in.style.size  = 20.0f;
    in.char_width  = fixed_cw(0.5f);  // each char = 10px at size 20
    in.box.enabled = true;
    // "one" = 3*10=30, "two" = 3*10=30 → 30+10+30=70; space = 10
    in.box.size    = Vec2{65.0f, 200.0f};  // tight: force line breaks

    auto r = TextLayoutEngine::layout(in);
    CHECK(r.lines.size() > 1);
    CHECK_FALSE(r.clipped);
}

TEST_CASE("TextLayout: single word longer than box stays on one line") {
    TextLayoutInput in;
    in.text        = "Superlongword";
    in.style.size  = 20.0f;
    in.char_width  = fixed_cw();
    in.box.enabled = true;
    in.box.size    = Vec2{50.0f, 200.0f};  // too narrow for the word

    auto r = TextLayoutEngine::layout(in);
    // Layout cannot break inside a word, so it stays on one line
    CHECK(r.lines.size() == 1);
    CHECK(r.lines[0].text == "Superlongword");
}

// ---------------------------------------------------------------------------
// max_lines
// ---------------------------------------------------------------------------
TEST_CASE("TextLayout: max_lines clips extra lines") {
    TextLayoutInput in;
    in.text             = "a b c d e f g h";
    in.style.size       = 20.0f;
    in.style.max_lines  = 2;
    in.char_width       = fixed_cw();
    in.box.enabled      = true;
    in.box.size         = Vec2{30.0f, 200.0f};  // force many breaks

    auto r = TextLayoutEngine::layout(in);
    CHECK(r.lines.size() <= 2);
    CHECK(r.clipped);
}

TEST_CASE("TextLayout: max_lines=0 means unlimited") {
    TextLayoutInput in;
    in.text             = "a b c d e f";
    in.style.size       = 20.0f;
    in.style.max_lines  = 0;
    in.char_width       = fixed_cw();
    in.box.enabled      = true;
    in.box.size         = Vec2{25.0f, 200.0f};  // one word per line

    auto r = TextLayoutEngine::layout(in);
    CHECK(r.lines.size() == 6);
    CHECK_FALSE(r.clipped);
}

// ---------------------------------------------------------------------------
// auto_scale
// ---------------------------------------------------------------------------
TEST_CASE("TextLayout: auto_scale reduces font to fit") {
    TextLayoutInput in;
    in.text              = "Hello World";
    in.style.size        = 100.0f;
    in.style.auto_scale  = true;
    in.style.min_size    = 10.0f;
    in.style.max_size    = 100.0f;
    in.style.max_lines   = 1;
    in.char_width        = fixed_cw();  // 50px per char at size 100
    in.box.enabled       = true;
    in.box.size          = Vec2{200.0f, 50.0f};  // "Hello World" = 11 chars

    auto r = TextLayoutEngine::layout(in);
    // "Hello World" at 100: 11 * 50 = 550px → doesn't fit in 200px
    // auto_scale should find smaller size where it fits
    CHECK(r.resolved_font_size < 100.0f);
    CHECK(r.lines.size() == 1);
}

// ---------------------------------------------------------------------------
// Fallback char_width (no function provided)
// ---------------------------------------------------------------------------
TEST_CASE("TextLayout: no char_width fn uses fallback estimate") {
    TextLayoutInput in;
    in.text        = "Hello";
    in.style.size  = 20.0f;
    // in.char_width not set → uses fallback

    auto r = TextLayoutEngine::layout(in);
    CHECK(r.lines.size() == 1);
    CHECK(r.resolved_font_size == doctest::Approx(20.0f));
}

// ---------------------------------------------------------------------------
// TextStyle new fields
// ---------------------------------------------------------------------------
TEST_CASE("TextStyle: new fields have expected defaults") {
    TextStyle s;
    CHECK(s.line_height == doctest::Approx(1.2f));
    CHECK(s.tracking    == doctest::Approx(0.0f));
    CHECK(s.max_lines   == 0);
    CHECK_FALSE(s.auto_scale);
    CHECK(s.min_size    == doctest::Approx(12.0f));
    CHECK(s.max_size    == doctest::Approx(256.0f));
}

TEST_CASE("TextBox: disabled by default") {
    TextBox box;
    CHECK_FALSE(box.enabled);
}
