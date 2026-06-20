#include <doctest/doctest.h>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <fstream>
using namespace chronon3d;

using namespace chronon3d::assets;

TEST_CASE("SVG path parser supports M L Z") {
    auto res = parse_svg_path_data("M 0 0 L 100 0 L 100 100 Z");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 4);
    CHECK(res.path.commands[0].type == PathCommandType::MoveTo);
    CHECK(res.path.commands[1].type == PathCommandType::LineTo);
    CHECK(res.path.commands[2].type == PathCommandType::LineTo);
    CHECK(res.path.commands[3].type == PathCommandType::Close);
    CHECK(res.path.closed);
}

TEST_CASE("SVG path parser supports H and V") {
    auto res = parse_svg_path_data("M 10 20 H 50 V 80");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 3);
    CHECK(res.path.commands[1].p0.x == doctest::Approx(50.0f));
    CHECK(res.path.commands[1].p0.y == doctest::Approx(20.0f));
    CHECK(res.path.commands[2].p0.x == doctest::Approx(50.0f));
    CHECK(res.path.commands[2].p0.y == doctest::Approx(80.0f));
}

TEST_CASE("SVG path parser supports cubic and quadratic curves") {
    auto res = parse_svg_path_data("M 0 0 C 10 0 20 10 30 30 Q 40 40 50 50");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 3);
    CHECK(res.path.commands[1].type == PathCommandType::CubicTo);
    CHECK(res.path.commands[2].type == PathCommandType::QuadraticTo);
}

TEST_CASE("SVG path parser supports relative commands") {
    auto res = parse_svg_path_data("M 10 10 l 5 0 v 5 h -5 z");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 5);
    CHECK(res.path.commands[1].p0.x == doctest::Approx(15.0f));
    CHECK(res.path.commands[1].p0.y == doctest::Approx(10.0f));
    CHECK(res.path.commands[2].p0.x == doctest::Approx(15.0f));
    CHECK(res.path.commands[2].p0.y == doctest::Approx(15.0f));
    CHECK(res.path.commands[3].p0.x == doctest::Approx(10.0f));
    CHECK(res.path.commands[3].p0.y == doctest::Approx(15.0f));
}

TEST_CASE("SVG path parser rejects unsupported commands") {
    auto res = parse_svg_path_data("M 0 0 A 10 10 0 0 1 20 20");
    CHECK_FALSE(res.ok);
    CHECK_FALSE(res.error.empty());
}

TEST_CASE("SVG path loader parses a minimal SVG file") {
    // Write a temporary valid SVG file
    const std::string filename = "temp_test_path.svg";
    {
        std::ofstream out(filename);
        out << R"(<svg width="100" height="100">)" << "\n"
            << R"(  <path d="M 10 10 L 50 50 Z" fill="red" />)" << "\n"
            << R"(</svg>)" << "\n";
    }

    auto res = load_svg_path_file(filename);
    
    // Clean up file
    std::remove(filename.c_str());

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 3);
    CHECK(res.path.commands[0].type == PathCommandType::MoveTo);
    CHECK(res.path.commands[1].type == PathCommandType::LineTo);
    CHECK(res.path.commands[2].type == PathCommandType::Close);
}
