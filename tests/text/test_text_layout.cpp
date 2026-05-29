#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_layout_engine.hpp>

using namespace chronon3d;

TEST_CASE("TextLayoutEngine layout V2 specifications") {
    auto default_char_width = [](char, float font_size) -> float {
        return font_size * 0.6f; // mock character width
    };

    SUBCASE("Empty text returns safe result") {
        TextLayoutInput input;
        input.text = "";
        input.style.size = 20.0f;
        input.char_width = default_char_width;

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "");
        CHECK(res.size.x == doctest::Approx(0.0f));
    }

    SUBCASE("Keeps manual newline") {
        TextLayoutInput input;
        input.text = "Line1\nLine2";
        input.style.size = 10.0f;
        input.char_width = default_char_width;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[0].text == "Line1");
        CHECK(res.lines[1].text == "Line2");
    }

    SUBCASE("Respects max_lines and adds ellipsis") {
        TextLayoutInput input;
        input.text = "One Two Three Four Five";
        input.style.size = 10.0f;
        input.style.max_lines = 2;
        input.style.ellipsis = true;
        input.style.wrap = TextWrap::Word;
        input.box.enabled = true;
        input.box.size = {65.0f, 100.0f}; // Should wrap each word roughly
        input.char_width = default_char_width;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[1].text.find("...") != std::string::npos);
    }

    SUBCASE("Respects text alignment center/right") {
        TextLayoutInput input;
        input.text = "A\nLongLine";
        input.style.size = 10.0f;
        input.char_width = default_char_width;
        input.style.align = TextAlign::Center;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        // "A" is shorter than "LongLine", so it should have a positive dx
        CHECK(res.lines[0].position.x > 0.0f);
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
    }

    SUBCASE("Auto fit shrinks text size to fit box") {
        TextLayoutInput input;
        input.text = "ExtremelyLongSentenceThatDoesNotFit";
        input.style.size = 100.0f;
        input.style.min_size = 10.0f;
        input.style.auto_fit = true;
        input.box.enabled = true;
        input.box.size = {120.0f, 100.0f};
        input.char_width = default_char_width;

        auto res = TextLayoutEngine::layout(input);
        // The layout size should fit inside the box limit
        CHECK(res.size.x <= 120.0f);
    }

    SUBCASE("Long word character wrapping does not crash") {
        TextLayoutInput input;
        input.text = "Supercalifragilisticexpialidocious";
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Word;
        input.box.enabled = true;
        input.box.size = {50.0f, 100.0f};
        input.char_width = default_char_width;

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() > 1);
        CHECK(res.size.x <= 50.0f);
    }

    SUBCASE("Spaces only") {
        TextLayoutInput input;
        input.text = "     ";
        input.style.size = 10.0f;
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "     ");
    }

    SUBCASE("Tabs only") {
        TextLayoutInput input;
        input.text = "\t\t";
        input.style.size = 10.0f;
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "\t\t");
    }

    SUBCASE("Trailing newline") {
        TextLayoutInput input;
        input.text = "Hello\n";
        input.style.size = 10.0f;
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        // "Hello\n" outputs "Hello" without crashing or generating an extra empty trailing line
        REQUIRE(res.lines.size() == 1);
        CHECK(res.lines[0].text == "Hello");
    }

    SUBCASE("Zero box dimensions") {
        TextLayoutInput input;
        input.text = "Hello World";
        input.style.size = 10.0f;
        input.box.enabled = true;
        input.box.size = {0.0f, 0.0f};
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        // With 0 box dimension, wrap should be disabled or safe
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "Hello World");
    }

    SUBCASE("TextWrap::None with max_lines") {
        TextLayoutInput input;
        input.text = "Line1\nLine2\nLine3";
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::None;
        input.style.max_lines = 2;
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
        CHECK(res.lines[0].text == "Line1");
        CHECK(res.lines[1].text == "Line2");
    }

    SUBCASE("TextWrap::Character with max_lines") {
        TextLayoutInput input;
        input.text = "abcdefgh";
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Character;
        input.style.max_lines = 2;
        input.box.enabled = true;
        input.box.size = {20.0f, 100.0f}; // box fits ~3 chars per line
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
    }

    SUBCASE("Ellipsis with extremely narrow box") {
        TextLayoutInput input;
        input.text = "abcdefgh";
        input.style.size = 10.0f;
        input.style.wrap = TextWrap::Character;
        input.style.max_lines = 1;
        input.style.ellipsis = true;
        input.box.enabled = true;
        input.box.size = {5.0f, 100.0f}; // too narrow for even "..."
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "...");
    }

    SUBCASE("Ellipsis with empty string") {
        TextLayoutInput input;
        input.text = "";
        input.style.size = 10.0f;
        input.style.max_lines = 1;
        input.style.ellipsis = true;
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "");
    }

    SUBCASE("Negative tracking") {
        TextLayoutInput input;
        input.text = "abc";
        input.style.size = 10.0f;
        input.style.tracking = -2.0f;
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        // Width of abc: 3 * (10*0.6) + 3*(-2) = 18 - 6 = 12
        CHECK(res.size.x == doctest::Approx(12.0f));
    }

    SUBCASE("Line heights extreme") {
        TextLayoutInput input;
        input.text = "a\nb";
        input.style.size = 10.0f;
        input.char_width = default_char_width;

        // Small line height (clamped to at least 1.0 pixel)
        input.style.line_height = 0.1f;
        auto res_small = TextLayoutEngine::layout(input);
        CHECK(res_small.size.y == doctest::Approx(2.0f));

        // Large line height
        input.style.line_height = 5.0f;
        auto res_large = TextLayoutEngine::layout(input);
        CHECK(res_large.size.y == doctest::Approx(100.0f));
    }

    SUBCASE("Auto-fit constraint handling") {
        TextLayoutInput input;
        input.text = "Hello World";
        input.style.size = 50.0f;
        input.style.min_size = 20.0f;
        input.style.max_size = 100.0f;
        input.style.auto_fit = true;
        input.box.enabled = true;
        input.box.size = {10.0f, 10.0f}; // Extremely small box
        input.char_width = default_char_width;
        auto res = TextLayoutEngine::layout(input);
        // Size should be constrained to min_size
        CHECK(res.size.y >= 20.0f);
    }

    SUBCASE("Right alignment offset") {
        TextLayoutInput input;
        input.text = "a\nabc";
        input.style.size = 10.0f;
        input.char_width = default_char_width;
        input.style.align = TextAlign::Right;
        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        // Width of "a" is 6, width of "abc" is 18.
        // Line 0 ("a") should start at 18 - 6 = 12.
        CHECK(res.lines[0].position.x == doctest::Approx(12.0f));
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
    }
}

