#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/math/transform.hpp>

using namespace chronon3d;

TEST_CASE("All builtin renderable shape types have software processors") {
    SoftwareRenderer renderer;
    const auto& registry = renderer.software_registry();

    CHECK(registry.get_shape(ShapeType::Rect) != nullptr);
    CHECK(registry.get_shape(ShapeType::RoundedRect) != nullptr);
    CHECK(registry.get_shape(ShapeType::Circle) != nullptr);
    CHECK(registry.get_shape(ShapeType::Line) != nullptr);
    CHECK(registry.get_shape(ShapeType::Image) != nullptr);

    CHECK(registry.get_shape(ShapeType::FakeBox3D) != nullptr);
    CHECK(registry.get_shape(ShapeType::GridPlane) != nullptr);
}
