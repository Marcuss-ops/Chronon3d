// Regression coverage for fullscreen_rect/fill with explicit canvas dimensions.

#include <doctest/doctest.h>

#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/shape/rect_shape.hpp>

using namespace chronon3d;

namespace {

const RenderNode& first_rect_node(const Layer& layer) {
    REQUIRE(layer.nodes.size() == 1);
    REQUIRE(layer.nodes[0].shape.type() == ShapeType::Rect);
    return layer.nodes[0];
}

void check_rect_geometry(const RenderNode& node,
                         Vec2 size,
                         Vec2 expected_position) {
    const RectShape& rect = node.shape.rect();
    CHECK(rect.size.x == doctest::Approx(size.x));
    CHECK(rect.size.y == doctest::Approx(size.y));
    CHECK(node.world_transform.position.x == doctest::Approx(expected_position.x));
    CHECK(node.world_transform.position.y == doctest::Approx(expected_position.y));
    CHECK(node.world_transform.position.z == doctest::Approx(0.0f));
}

} // namespace

TEST_CASE("fullscreen_rect uses explicit 1920x1080 dimensions") {
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& layer) {
        layer.screen_dimensions(1920.0f, 1080.0f);
        layer.fullscreen_rect("fill", Color::white());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    CHECK(layer.layout.enabled);
    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);
    CHECK(layer.layout.margin == doctest::Approx(0.0f));

    const RenderNode& node = first_rect_node(layer);
    check_rect_geometry(node, {1920.0f, 1080.0f}, {-960.0f, -540.0f});
    CHECK(node.name == "fill");
    CHECK(node.color == Color::white());
}

TEST_CASE("fullscreen_rect honors explicit 1280x720 dimensions") {
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& layer) {
        layer.screen_dimensions(1280.0f, 720.0f);
        layer.fullscreen_rect("fill_bg", Color::black());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);

    const RenderNode& node = first_rect_node(layer);
    check_rect_geometry(node, {1280.0f, 720.0f}, {-640.0f, -360.0f});
    CHECK(node.name == "fill_bg");
    CHECK(node.color == Color::black());
}

TEST_CASE("fill delegates to fullscreen_rect with explicit dimensions") {
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& layer) {
        layer.screen_dimensions(1920.0f, 1080.0f);
        layer.fill(Color::red());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);

    const RenderNode& node = first_rect_node(layer);
    check_rect_geometry(node, {1920.0f, 1080.0f}, {-960.0f, -540.0f});
    CHECK(node.name == "fill");
    CHECK(node.color == Color::red());
}
