#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_layout_result.hpp>

using namespace chronon3d;
using namespace chronon3d::text;

TEST_CASE("layout_x_start: Left align → x_start is 0") {
    CHECK(layout_x_start(200.0f, TextAlign::Left) == doctest::Approx(0.0f));
}

TEST_CASE("layout_x_start: Center align → x_start is -half_width") {
    CHECK(layout_x_start(200.0f, TextAlign::Center) == doctest::Approx(-100.0f));
}

TEST_CASE("layout_x_start: Right align → x_start is -total_width") {
    CHECK(layout_x_start(200.0f, TextAlign::Right) == doctest::Approx(-200.0f));
}

TEST_CASE("layout_x_start: zero width → x_start always 0") {
    CHECK(layout_x_start(0.0f, TextAlign::Left)   == doctest::Approx(0.0f));
    CHECK(layout_x_start(0.0f, TextAlign::Center) == doctest::Approx(0.0f));
    CHECK(layout_x_start(0.0f, TextAlign::Right)  == doctest::Approx(0.0f));
}

TEST_CASE("TextLayoutResult: default construction is sane") {
    TextLayoutResult r;
    CHECK(r.total_width  == doctest::Approx(0.0f));
    CHECK(r.x_start      == doctest::Approx(0.0f));
    CHECK(r.ascent       == doctest::Approx(0.0f));
    CHECK(r.descent      == doctest::Approx(0.0f));
    CHECK(r.line_height  == doctest::Approx(0.0f));
}
