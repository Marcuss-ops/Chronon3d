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
