#include <doctest/doctest.h>

#include <chronon3d/scene/render_node_factory.hpp>

using namespace chronon3d;

TEST_CASE("RenderNodeFactory creates rect nodes with centered anchors") {
    auto* res = std::pmr::get_default_resource();

    auto node = RenderNodeFactory::rect(
        res,
        "box",
        RectParams{
            .size = {120.0f, 80.0f},
            .color = Color::red(),
            .pos = {10.0f, 20.0f, 30.0f}
        }
    );

    CHECK(node.name == "box");
    CHECK(node.shape.type == ShapeType::Rect);
    CHECK(node.shape.rect.size.x == doctest::Approx(120.0f));
    CHECK(node.shape.rect.size.y == doctest::Approx(80.0f));
    CHECK(node.world_transform.position.x == doctest::Approx(10.0f));
    CHECK(node.world_transform.position.y == doctest::Approx(20.0f));
    CHECK(node.world_transform.position.z == doctest::Approx(30.0f));
    CHECK(node.world_transform.anchor.x == doctest::Approx(60.0f));
    CHECK(node.world_transform.anchor.y == doctest::Approx(40.0f));
    CHECK(node.color.r == doctest::Approx(Color::red().r));
}

TEST_CASE("RenderNodeFactory creates grid background nodes") {
    auto* res = std::pmr::get_default_resource();

    auto node = RenderNodeFactory::grid_background(
        res,
        "grid",
        GridBackgroundParams{
            .size = {1920.0f, 1080.0f},
            .offset = {24.0f, 8.0f},
            .bg_color = Color{0.01f, 0.02f, 0.04f, 1.0f},
            .grid_color = Color{0.3f, 0.5f, 0.9f, 0.08f},
            .spacing = 96.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        }
    );

    CHECK(node.shape.type == ShapeType::GridBackground);
    CHECK(node.shape.grid_background.size.x == doctest::Approx(1920.0f));
    CHECK(node.shape.grid_background.offset.x == doctest::Approx(24.0f));
    CHECK(node.world_transform.position.x == doctest::Approx(0.0f));
    CHECK(node.world_transform.anchor.x == doctest::Approx(0.0f));
}
