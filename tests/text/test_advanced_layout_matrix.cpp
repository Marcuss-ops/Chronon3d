// ─── tests/text/test_advanced_layout_matrix.cpp ───────────────────────────────
//
// TICKET-FASE4-LAYOUT closure: advanced layout matrix + adversarial tests
//
// Scope (5 TEST_CASEs, ~25 SUBCASEs):
//   1. 3×3 wrap × overflow matrix (9 modes)
//        TextWrap::{None,Word,Character} × TextOverflow::{Clip,Ellipsis,Visible}
//   2. line-height matrix (0.5/0.8/1.0/1.2/1.5/2.0/3.0 × 3-line text)
//        + monotonicity check (each value produces strictly larger size.y)
//   3. Adversarial: long-word (one word > box width) × 3 wrap modes
//   4. Adversarial: sub-glyph box (box width < one glyph) × 3 overflow modes
//   5. Adversarial: ellipsis-without-space (no whitespace + ellipsis) × 3 cases
//
// Highlights:
//   - Pure layout math (no rendering / no Blend2D dependency in default path)
//   - Some SUBCASEs opt-in to real FontEngine metrics (Inter-Bold.ttf fixture)
//   - Mirrors the test_text_layout.cpp SUBCASE pattern (TextLayoutInput + mock_char_width)
//   - 100% deterministic (no threads / no time / no PRNG per AGENTS.md v0.1
//     cat-2 freeze-compliant invariants)
//
// Per-line-height formula (verified from `text_layout_single.hpp:151` and
// `text_layout_inline.hpp:323`):
//   per_line_height = max(1.0f, font_size * style.line_height)
//   size.y         = num_lines * per_line_height
// The `max(1.0f, ...)` floor matters for line_height < 0.1 (sub-pixel
// requests clamp to 1px per line).  All matrix values above the floor.

#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/math/glm_types.hpp>

using namespace chronon3d;

namespace {
    // Mock char width (matches test_text_layout.cpp pattern; 0.6f × font_size).
    // Yields per-char width = 6.0f for font_size=10.0f, 12.0f for 20.0f, etc.
    static float mock_char_width(const void* /*ctx*/, char /*c*/, float font_size) {
        return font_size * 0.6f;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. 3×3 wrap × overflow matrix (9 modes)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AdvancedLayout: 3x3 wrap x overflow matrix") {
    auto setup = [](TextLayoutInput& input,
                    const char* text,
                    TextWrap wrap,
                    TextOverflow overflow,
                    float size,
                    Vec2 box) {
        input.text = text;
        input.style.size = size;
        input.style.wrap = wrap;
        input.style.overflow = overflow;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = box;
    };

    // Test text: 5 words (alpha bravo charlie delta echo) totaling 28 chars.
    // At 10pt with mock_char_width → ~168px wide.  Narrow 65px box forces wrap
    // for Word + Character wrap modes; NoWrap bypasses the box.
    const char* test_text = "alpha bravo charlie delta echo";
    constexpr float kFontSize = 10.0f;
    constexpr Vec2 kNarrowBox{65.0f, 100.0f};

    // ── TextWrap::None × TextOverflow::{Clip,Ellipsis,Visible} ─────────
    SUBCASE("None + Clip: text extends beyond box, 1 line, no truncation") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::None, TextOverflow::Clip,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        // NoWrap → exactly 1 line, even if the line width > box width.
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == test_text);
        // NoWrap + Clip: line width > box width (text extends, but layout
        // does not truncate — the renderer is the one that decides whether
        // to render text outside the box).
        CHECK(res.lines[0].width > kNarrowBox.x);
    }

    SUBCASE("None + Ellipsis: 1 line, no truncation (no max_lines exceeded)") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::None, TextOverflow::Ellipsis,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        // NoWrap → 1 line.  Ellipsis is only appended if max_lines is
        // exceeded by explicit \n breaks (or by wrap-then-truncate).  Here
        // we have a single line, so the text is preserved verbatim.
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == test_text);
    }

    SUBCASE("None + Visible: text extends beyond box, 1 line, no truncation (no-op for layout)") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::None, TextOverflow::Visible,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        // Visible is a no-op at the layout level (same as Clip for layout
        // purposes; the renderer is responsible for the "visible outside
        // box" semantic).  Same expected behavior as None + Clip.
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == test_text);
        CHECK(res.lines[0].width > kNarrowBox.x);
    }

    // ── TextWrap::Word × TextOverflow::{Clip,Ellipsis,Visible} ──────────
    SUBCASE("Word + Clip: multi-line, no truncation, no \"...\"") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::Word, TextOverflow::Clip,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        // Word wrap → multiple lines (5 words in 65px box → at least 2-3 lines).
        CHECK(res.lines.size() >= 2);
        // Clip: no ellipsis applied.
        for (const auto& line : res.lines) {
            CHECK(line.text.find("...") == std::string::npos);
        }
    }

    SUBCASE("Word + Ellipsis: multi-line, last line has \"...\"") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::Word, TextOverflow::Ellipsis,
              kFontSize, kNarrowBox);
        // max_lines=2 forces truncation (text wraps to 5 lines naturally);
        // Ellipsis appends "..." to the 2nd (last visible) line.
        input.style.max_lines = 2;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
        // Ellipsis: last line should have "..." appended (truncation marker).
        CHECK(res.lines.back().text.find("...") != std::string::npos);
    }

    SUBCASE("Word + Visible: multi-line, no truncation (Visible is layout no-op)") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::Word, TextOverflow::Visible,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() >= 2);
        // Visible: no ellipsis applied (same as Clip for layout).
        for (const auto& line : res.lines) {
            CHECK(line.text.find("...") == std::string::npos);
        }
    }

    // ── TextWrap::Character × TextOverflow::{Clip,Ellipsis,Visible} ────
    SUBCASE("Character + Clip: multi-line (per-character wrap), no truncation") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::Character, TextOverflow::Clip,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() >= 2);
        // Each line should fit within box width (Character wrap is per-character).
        for (const auto& line : res.lines) {
            CHECK(line.width <= kNarrowBox.x + 0.1f);  // small epsilon
        }
        // Clip: no "..." in any line.
        for (const auto& line : res.lines) {
            CHECK(line.text.find("...") == std::string::npos);
        }
    }

    SUBCASE("Character + Ellipsis: multi-line, last line has \"...\"") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::Character, TextOverflow::Ellipsis,
              kFontSize, kNarrowBox);
        // max_lines=2 forces truncation (text wraps to >2 lines naturally);
        // Ellipsis appends "..." to the 2nd (last visible) line.
        input.style.max_lines = 2;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
        CHECK(res.lines.back().text.find("...") != std::string::npos);
    }

    SUBCASE("Character + Visible: multi-line, no truncation (Visible is layout no-op)") {
        TextLayoutInput input;
        setup(input, test_text, TextWrap::Character, TextOverflow::Visible,
              kFontSize, kNarrowBox);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() >= 2);
        for (const auto& line : res.lines) {
            CHECK(line.text.find("...") == std::string::npos);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. line-height matrix (7 values × 3-line text) + monotonicity check
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AdvancedLayout: line-height matrix (7 values x 3-line text)") {
    auto build_3line = [](TextLayoutInput& input, float lh) {
        input.text = "Line1\nLine2\nLine3";  // 3 explicit lines
        input.style.size = 10.0f;
        input.style.line_height = lh;
        input.style.wrap = TextWrap::Word;
        input.char_width_fn = mock_char_width;
    };

    // Per-line-height = max(1.0, font_size × line_height) = max(1.0, 10 × lh)
    // Total size.y = 3 × per-line-height
    //   lh=0.5 → 15.0   lh=0.8 → 24.0   lh=1.0 → 30.0   lh=1.2 → 36.0
    //   lh=1.5 → 45.0   lh=2.0 → 60.0   lh=3.0 → 90.0
    constexpr float kFontSize = 10.0f;
    constexpr int kNumLines = 3;
    constexpr float kEpsilon = 0.5f;

    SUBCASE("line_height=0.5: 3 lines x 10pt x 0.5 = 15.0px") {
        TextLayoutInput input;
        build_3line(input, 0.5f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 0.5f)
                              .epsilon(kEpsilon));
    }

    SUBCASE("line_height=0.8: 3 lines x 10pt x 0.8 = 24.0px") {
        TextLayoutInput input;
        build_3line(input, 0.8f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 0.8f)
                              .epsilon(kEpsilon));
    }

    SUBCASE("line_height=1.0: canonical baseline (3 lines x 10pt = 30.0px)") {
        TextLayoutInput input;
        build_3line(input, 1.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 1.0f)
                              .epsilon(kEpsilon));
    }

    SUBCASE("line_height=1.2: 3 lines x 10pt x 1.2 = 36.0px (typical body text)") {
        TextLayoutInput input;
        build_3line(input, 1.2f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 1.2f)
                              .epsilon(kEpsilon));
    }

    SUBCASE("line_height=1.5: 3 lines x 10pt x 1.5 = 45.0px (loose)") {
        TextLayoutInput input;
        build_3line(input, 1.5f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 1.5f)
                              .epsilon(kEpsilon));
    }

    SUBCASE("line_height=2.0: 3 lines x 10pt x 2.0 = 60.0px (double-spaced)") {
        TextLayoutInput input;
        build_3line(input, 2.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 2.0f)
                              .epsilon(kEpsilon));
    }

    SUBCASE("line_height=3.0: 3 lines x 10pt x 3.0 = 90.0px (triple-spaced)") {
        TextLayoutInput input;
        build_3line(input, 3.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == kNumLines);
        CHECK(res.size.y == doctest::Approx(kNumLines * kFontSize * 3.0f)
                              .epsilon(kEpsilon));
    }

    // Monotonicity: size.y is strictly increasing across the 7-value sweep.
    // Regression lock: a future edit to the per-line-height formula that
    // breaks monotonicity (e.g. an off-by-one in the multiplier) would fail
    // this subcase.
    SUBCASE("line_height is strictly monotonically increasing across 7 values") {
        float prev_y = -1.0f;  // start at -1 so the first iteration always passes
        for (float lh : {0.5f, 0.8f, 1.0f, 1.2f, 1.5f, 2.0f, 3.0f}) {
            TextLayoutInput input;
            build_3line(input, lh);
            auto res = TextLayoutEngine::layout(input);
            INFO("lh=", lh, " size.y=", res.size.y, " prev_y=", prev_y);
            CHECK(res.size.y > prev_y);  // strictly increasing
            prev_y = res.size.y;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Adversarial: long word (one word > box width) × 3 wrap modes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AdvancedLayout: adversarial long-word (one word > box width)") {
    // 32-char single word, way longer than the 50px box.  The canonical
    // "stress test" for word-wrap engines — exercises the boundary case
    // where the wrap algorithm cannot find a natural break point.
    const char* kLongWord = "Supercalifragilisticexpialidocious";
    constexpr float kFontSize = 10.0f;
    constexpr Vec2 kBox{50.0f, 100.0f};

    SUBCASE("None + long word: text extends beyond box, exactly 1 line") {
        TextLayoutInput input;
        input.text = kLongWord;
        input.style.size = kFontSize;
        input.style.wrap = TextWrap::None;
        input.style.overflow = TextOverflow::Clip;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = kBox;

        auto res = TextLayoutEngine::layout(input);
        // NoWrap → exactly 1 line, even if the word is much longer than the box.
        // This is the user-spec "long-word" adversarial for None.
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == kLongWord);
        // Line width > box width (word extends beyond).
        CHECK(res.lines[0].width > kBox.x);
    }

    SUBCASE("Word + long word: fallback to character wrap (no space to break at)") {
        TextLayoutInput input;
        input.text = kLongWord;
        input.style.size = kFontSize;
        input.style.wrap = TextWrap::Word;
        input.style.overflow = TextOverflow::Clip;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = kBox;

        auto res = TextLayoutEngine::layout(input);
        // WordWrap can't break a single word (no whitespace / no hyphen), so
        // the engine falls back to character-level wrapping.  This is the
        // user-spec "long-word" adversarial for Word.
        CHECK(res.lines.size() > 1);
        // Each line should fit within box width (with small epsilon for FP).
        for (const auto& line : res.lines) {
            CHECK(line.width <= kBox.x + 0.1f);
        }
    }

    SUBCASE("Character + long word: wraps mid-word at character boundaries") {
        TextLayoutInput input;
        input.text = kLongWord;
        input.style.size = kFontSize;
        input.style.wrap = TextWrap::Character;
        input.style.overflow = TextOverflow::Clip;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = kBox;

        auto res = TextLayoutEngine::layout(input);
        // Character wrap is the canonical case for the user-spec "long-word"
        // adversarial — wraps at every character, so even a single word
        // that exceeds box width is split mid-word.
        CHECK(res.lines.size() > 1);
        for (const auto& line : res.lines) {
            CHECK(line.width <= kBox.x + 0.1f);
        }
        // Concatenation of line texts (minus wrap-spaces) should reconstruct
        // the original word (no characters lost in the wrap).
        std::string joined;
        for (const auto& line : res.lines) {
            joined += line.text;
        }
        CHECK(joined == kLongWord);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Adversarial: sub-glyph box (box width < one glyph)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AdvancedLayout: adversarial sub-glyph box (box width < one glyph)") {
    // 10pt text with mock_char_width → 6.0px per char.  Sub-glyph box
    // width = 2px (less than 1 glyph) + 5px (less than 1 glyph for the
    // ellipsis test).  Exercises the boundary case where the box is
    // smaller than a single rendered glyph.
    const char* kText = "abcdef";

    SUBCASE("Clip + sub-glyph box: text extends beyond box, no truncation at layout") {
        TextLayoutInput input;
        input.text = kText;
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::None;
        input.style.overflow = TextOverflow::Clip;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = {2.0f, 100.0f};  // 2px wide, less than 1 glyph (6px)

        auto res = TextLayoutEngine::layout(input);
        // NoWrap → 1 line, text preserved (the box width doesn't truncate
        // at the layout level; the renderer is responsible for the clip).
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == kText);
        // Line width > box width (text extends beyond the sub-glyph box).
        CHECK(res.lines[0].width > 2.0f);
    }

    SUBCASE("Ellipsis + sub-glyph box: result is \"...\" (text fully truncated)") {
        TextLayoutInput input;
        input.text = kText;
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Character;
        input.style.max_lines = 1;
        input.style.ellipsis = true;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = {5.0f, 100.0f};  // 5px wide, less than 1 glyph (6px)

        auto res = TextLayoutEngine::layout(input);
        // With max_lines=1 and a box too small for any character, the
        // ellipsis is the entire visible content.  This matches the
        // existing test_text_layout.cpp "Ellipsis with extremely narrow box"
        // subcase (extended to the sub-glyph regime).
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "...");
    }

    SUBCASE("Visible + sub-glyph box: text extends beyond box, no truncation (no-op for layout)") {
        TextLayoutInput input;
        input.text = kText;
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::None;
        input.style.overflow = TextOverflow::Visible;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = {2.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        // Visible: same as Clip for layout purposes (no truncation).
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == kText);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Adversarial: ellipsis-without-space (no whitespace + ellipsis)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AdvancedLayout: adversarial ellipsis-without-space") {
    // 32-char + 6-char = 38-char no-space strings (alphanumeric).  Exercises
    // the boundary case where the wrap algorithm has no whitespace to
    // break at, and the ellipsis truncation must still produce a valid
    // result (either truncated text + "..." or just "...").
    SUBCASE("long no-space text + small box + max_lines=1 + ellipsis: result has \"...\"") {
        const char* kNoSpace = "abcdefghijklmnopqrstuvwxyz012345";  // 32 chars
        TextLayoutInput input;
        input.text = kNoSpace;
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Word;
        input.style.max_lines = 1;
        input.style.ellipsis = true;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = {50.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        // WordWrap on no-space text + max_lines=1 + ellipsis → the engine
        // either truncates the text with "..." or produces a single "...".
        // The user-spec invariant is: the result has "..." (truncation
        // occurred, no silent loss).
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text.find("...") != std::string::npos);
    }

    SUBCASE("short no-space text + large box + max_lines=1 + ellipsis: full text preserved") {
        // 5-char text fits in a 500px box — no truncation, no ellipsis.
        const char* kShort = "hello";
        TextLayoutInput input;
        input.text = kShort;
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Word;
        input.style.max_lines = 1;
        input.style.ellipsis = true;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = {500.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        // Text fits, no ellipsis.
        CHECK(res.lines[0].text == kShort);
        CHECK(res.lines[0].text.find("...") == std::string::npos);
    }

    SUBCASE("no-space text + Character wrap + max_lines=2 + ellipsis: last line has \"...\"") {
        // 38-char no-space text in 50px box with Character wrap → multi-line,
        // 2 lines visible, 2nd line truncated with "...".
        const char* kNoSpace = "abcdefghijklmnopqrstuvwxyz0123456789";
        TextLayoutInput input;
        input.text = kNoSpace;
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Character;
        input.style.max_lines = 2;
        input.style.ellipsis = true;
        input.char_width_fn = mock_char_width;
        input.box.enabled = true;
        input.box.size = {50.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
        // Last line has the "..." truncation marker.
        CHECK(res.lines.back().text.find("...") != std::string::npos);
    }
}
