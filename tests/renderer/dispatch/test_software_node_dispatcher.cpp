#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/math/transform.hpp>

using namespace chronon3d;

namespace {
static float sum_rgb(const Framebuffer& fb) {
    float sum = 0.0f;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const auto p = fb.get_pixel(x, y);
            sum += p.r + p.g + p.b;
        }
    }
    return sum;
}
}

TEST_CASE("All builtin renderable shape types have software processors") {
    SoftwareRenderer renderer;
    const auto& registry = renderer.software_registry();

    CHECK(registry.get_shape(ShapeType::Rect) != nullptr);
    CHECK(registry.get_shape(ShapeType::RoundedRect) != nullptr);
    CHECK(registry.get_shape(ShapeType::Circle) != nullptr);
    CHECK(registry.get_shape(ShapeType::Line) != nullptr);
    CHECK(registry.get_shape(ShapeType::Text) != nullptr);
    CHECK(registry.get_shape(ShapeType::Image) != nullptr);

    CHECK(registry.get_shape(ShapeType::FakeExtrudedText) != nullptr);
    CHECK(registry.get_shape(ShapeType::FakeBox3D) != nullptr);
    CHECK(registry.get_shape(ShapeType::GridPlane) != nullptr);
}

TEST_CASE("SoftwareNodeDispatcher does not use legacy fallback when processor is missing") {
    SoftwareRenderer renderer;
    renderer::SoftwareRegistry empty_registry;

    Framebuffer fb(120, 80);
    fb.clear(Color{0, 0, 0, 1});

    RenderNode node;
    node.name = "text-without-processor";
    node.shape.type = ShapeType::Text;
    node.shape.text.text = "TEST";
    node.shape.text.style.font_path = "assets/fonts/Inter-Bold.ttf";
    node.shape.text.style.size = 32.0f;
    node.shape.text.style.color = Color{1, 1, 1, 1};

    RenderState state;
    state.matrix = math::translate(Vec3{60, 40, 0});
    state.opacity = 1.0f;

    Camera camera;

    const float before = sum_rgb(fb);

    // This should log an error to cerr but NOT render anything
    SoftwareNodeDispatcher::draw_node(
        renderer,
        fb,
        node,
        state,
        camera,
        fb.width(),
        fb.height(),
        empty_registry
    );

    const float after = sum_rgb(fb);

    CHECK(after == doctest::Approx(before));
}
