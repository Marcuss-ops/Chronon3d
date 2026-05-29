#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_layout_engine.hpp>

using namespace chronon3d;

static float mock_char_width(const void* /*ctx*/, char /*c*/, float font_size) {
    return font_size * 0.6f;
}

TEST_CASE("TextLayoutEngine layout V2 specifications") {
    auto setup_input = [](TextLayoutInput& input, const char* text, float size) {
        input.text = text;
        input.style.size = size;
        input.char_width_fn = mock_char_width;
    };

    SUBCASE("Empty text returns safe result") {
        TextLayoutInput input;
        setup_input(input, "", 20.0f);

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "");
        CHECK(res.size.x == doctest::Approx(0.0f));
    }

    SUBCASE("Keeps manual newline") {
        TextLayoutInput input;
        setup_input(input, "Line1\nLine2", 10.0f);

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[0].text == "Line1");
        CHECK(res.lines[1].text == "Line2");
    }

    SUBCASE("Respects max_lines and adds ellipsis") {
        TextLayoutInput input;
        setup_input(input, "One Two Three Four Five", 10.0f);
        input.style.max_lines = 2;
        input.style.ellipsis = true;
        input.style.wrap = TextWrap::Word;
        input.box.enabled = true;
        input.box.size = {65.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[1].text.find("...") != std::string::npos);
    }

    SUBCASE("Respects text alignment center/right") {
        TextLayoutInput input;
        setup_input(input, "A\nLongLine", 10.0f);
        input.style.align = TextAlign::Center;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[0].position.x > 0.0f);
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
    }

    SUBCASE("Auto fit shrinks text size to fit box") {
        TextLayoutInput input;
        setup_input(input, "ExtremelyLongSentenceThatDoesNotFit", 100.0f);
        input.style.min_size = 10.0f;
        input.style.auto_fit = true;
        input.box.enabled = true;
        input.box.size = {120.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x <= 120.0f);
    }

    SUBCASE("Long word character wrapping does not crash") {
        TextLayoutInput input;
        setup_input(input, "Supercalifragilisticexpialidocious", 10.0f);
        input.style.wrap = TextWrap::Word;
        input.box.enabled = true;
        input.box.size = {50.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() > 1);
        CHECK(res.size.x <= 50.0f);
    }

    SUBCASE("Spaces only") {
        TextLayoutInput input;
        setup_input(input, "     ", 10.0f);

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "     ");
    }

    SUBCASE("Tabs only") {
        TextLayoutInput input;
        setup_input(input, "\t\t", 10.0f);

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "\t\t");
    }

    SUBCASE("Trailing newline") {
        TextLayoutInput input;
        setup_input(input, "Hello\n", 10.0f);

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 1);
        CHECK(res.lines[0].text == "Hello");
    }

    SUBCASE("Zero box dimensions") {
        TextLayoutInput input;
        setup_input(input, "Hello World", 10.0f);
        input.box.enabled = true;
        input.box.size = {0.0f, 0.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "Hello World");
    }

    SUBCASE("TextWrap::None with max_lines") {
        TextLayoutInput input;
        setup_input(input, "Line1\nLine2\nLine3", 10.0f);
        input.style.wrap = TextWrap::None;
        input.style.max_lines = 2;

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
        CHECK(res.lines[0].text == "Line1");
        CHECK(res.lines[1].text == "Line2");
    }

    SUBCASE("TextWrap::Character with max_lines") {
        TextLayoutInput input;
        setup_input(input, "abcdefgh", 10.0f);
        input.style.wrap = TextWrap::Character;
        input.style.max_lines = 2;
        input.box.enabled = true;
        input.box.size = {20.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
    }

    SUBCASE("Ellipsis with extremely narrow box") {
        TextLayoutInput input;
        setup_input(input, "abcdefgh", 10.0f);
        input.style.wrap = TextWrap::Character;
        input.style.max_lines = 1;
        input.style.ellipsis = true;
        input.box.enabled = true;
        input.box.size = {5.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "...");
    }

    SUBCASE("Ellipsis with empty string") {
        TextLayoutInput input;
        setup_input(input, "", 10.0f);
        input.style.max_lines = 1;
        input.style.ellipsis = true;

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.lines[0].text == "");
    }

    SUBCASE("Negative tracking") {
        TextLayoutInput input;
        setup_input(input, "abc", 10.0f);
        input.style.tracking = -2.0f;

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(12.0f));
    }

    SUBCASE("Line heights extreme") {
        TextLayoutInput input;
        setup_input(input, "a\nb", 10.0f);

        input.style.line_height = 0.1f;
        auto res_small = TextLayoutEngine::layout(input);
        CHECK(res_small.size.y == doctest::Approx(2.0f));

        input.style.line_height = 5.0f;
        auto res_large = TextLayoutEngine::layout(input);
        CHECK(res_large.size.y == doctest::Approx(100.0f));
    }

    SUBCASE("Auto-fit constraint handling") {
        TextLayoutInput input;
        setup_input(input, "Hello World", 50.0f);
        input.style.min_size = 20.0f;
        input.style.max_size = 100.0f;
        input.style.auto_fit = true;
        input.box.enabled = true;
        input.box.size = {10.0f, 10.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.y >= 20.0f);
    }

    SUBCASE("Right alignment offset") {
        TextLayoutInput input;
        setup_input(input, "a\nabc", 10.0f);
        input.style.align = TextAlign::Right;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[0].position.x == doctest::Approx(12.0f));
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
    }
}

