#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_layout_helpers.hpp>

using namespace chronon3d;
using namespace chronon3d::detail::text_layout;

TEST_CASE("Text layout geometry normalizes size, line height, and wrapping") {
    TextLayoutInput input;
    input.style.size = 0.0f;
    input.style.line_height = 0.0f;
    input.style.wrap = TextWrap::Word;
    input.box.enabled = true;
    input.box.size = {100.0f, 20.0f};

    const auto geometry = layout_geometry(input);
    CHECK(geometry.font_size == doctest::Approx(1.0f));
    CHECK(geometry.line_height == doctest::Approx(1.0f));
    CHECK(geometry.max_width == doctest::Approx(100.0f));
    CHECK(geometry.wrapping_enabled);
}

TEST_CASE("Text layout alignment uses the authored width") {
    CHECK(aligned_line_x(TextAlign::Left, 20.0f, 100.0f) == doctest::Approx(0.0f));
    CHECK(aligned_line_x(TextAlign::Center, 20.0f, 100.0f) == doctest::Approx(40.0f));
    CHECK(aligned_line_x(TextAlign::Right, 20.0f, 100.0f) == doctest::Approx(80.0f));
}

TEST_CASE("previous_grapheme_start correctly identifies last grapheme boundary") {
    CHECK(!detail::previous_grapheme_start("").has_value());
    
    // Pure ASCII
    auto p1 = detail::previous_grapheme_start("abc");
    REQUIRE(p1.has_value());
    CHECK(*p1 == 2);
    
    // Multi-byte character at the end (è is 2 bytes: 0xC3 0xA8)
    std::string s2 = "abcè";
    auto p2 = detail::previous_grapheme_start(s2);
    REQUIRE(p2.has_value());
    CHECK(*p2 == 3);
    s2.resize(*p2);
    CHECK(s2 == "abc");
    
    // Multi-byte at start, single byte at end
    std::string s3 = "èabc";
    auto p3 = detail::previous_grapheme_start(s3);
    REQUIRE(p3.has_value());
    CHECK(*p3 == 4); // "è" is 2 bytes, 'a' is 1, 'b' is 1, 'c' is at offset 4
    s3.resize(*p3);
    CHECK(s3 == "èab");
}

