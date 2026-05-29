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
}
