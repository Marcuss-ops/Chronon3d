#include <doctest/doctest.h>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::assets;

TEST_CASE("SvgPathLoader - Parse MoveTo and LineTo") {
    auto path = parse_svg_path_data("M 10 10 L 20 20");
    REQUIRE(path.commands.size() == 2);
    
    CHECK(path.commands[0].type == SvgPathCommandType::MoveTo);
    CHECK(path.commands[0].p0.x == 10.0f);
    CHECK(path.commands[0].p0.y == 10.0f);

    CHECK(path.commands[1].type == SvgPathCommandType::LineTo);
    CHECK(path.commands[1].p0.x == 20.0f);
    CHECK(path.commands[1].p0.y == 20.0f);
}

TEST_CASE("SvgPathLoader - Parse Implicit LineTo after MoveTo") {
    auto path = parse_svg_path_data("M 0,0 10,10 20,20");
    REQUIRE(path.commands.size() == 3);
    CHECK(path.commands[0].type == SvgPathCommandType::MoveTo);
    CHECK(path.commands[1].type == SvgPathCommandType::LineTo);
    CHECK(path.commands[2].type == SvgPathCommandType::LineTo);
    CHECK(path.commands[2].p0.x == 20.0f);
    CHECK(path.commands[2].p0.y == 20.0f);
}

TEST_CASE("SvgPathLoader - Parse H and V") {
    auto path = parse_svg_path_data("M 0 0 H 10 V 20");
    REQUIRE(path.commands.size() == 3);
    
    CHECK(path.commands[1].type == SvgPathCommandType::LineTo);
    CHECK(path.commands[1].p0.x == 10.0f);
    CHECK(path.commands[1].p0.y == 0.0f); // inherits y

    CHECK(path.commands[2].type == SvgPathCommandType::LineTo);
    CHECK(path.commands[2].p0.x == 10.0f); // inherits x
    CHECK(path.commands[2].p0.y == 20.0f);
}

TEST_CASE("SvgPathLoader - Parse C, Q, Z") {
    auto path = parse_svg_path_data("M 0 0 C 1 2 3 4 5 6 Q 7 8 9 10 Z");
    REQUIRE(path.commands.size() == 4);
    
    CHECK(path.commands[1].type == SvgPathCommandType::CubicTo);
    CHECK(path.commands[1].p0.x == 1.0f);
    CHECK(path.commands[1].p2.x == 5.0f);

    CHECK(path.commands[2].type == SvgPathCommandType::QuadTo);
    CHECK(path.commands[2].p0.x == 7.0f);
    CHECK(path.commands[2].p1.y == 10.0f);

    CHECK(path.commands[3].type == SvgPathCommandType::Close);
}

TEST_CASE("SvgPathLoader - Unsupported command throws") {
    CHECK_THROWS_AS(parse_svg_path_data("M 0 0 A 1 1 0 0 0 1 1"), std::runtime_error);
}
