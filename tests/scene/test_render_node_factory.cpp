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
