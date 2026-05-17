#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

TEST_CASE("Shape model and SceneBuilder") {
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Rect node has ShapeType::Rect") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("test-rect", {.size={20, 20}, .color=Color::white(), .pos={0, 0, 0}});
            return s.build();
        });
        auto scene = comp.evaluate(0);
        const auto& nodes = scene.nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].shape.type == ShapeType::Rect);
        CHECK(nodes[0].shape.rect.size.x == 20.0f);
        CHECK(nodes[0].shape.rect.size.y == 20.0f);
    }

    SUBCASE("Circle node has ShapeType::Circle") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.circle("test-circle", {.radius=15.0f, .color=Color::white(), .pos={0, 0, 0}});
            return s.build();
        });
        auto scene = comp.evaluate(0);
        const auto& nodes = scene.nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].shape.type == ShapeType::Circle);
        CHECK(nodes[0].shape.circle.radius == 15.0f);
    }

    SUBCASE("Line node has ShapeType::Line") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.line("test-line", {.from={0, 0, 0}, .to={10, 10, 0}, .color=Color::white()});
            return s.build();
        });
        auto scene = comp.evaluate(0);
        const auto& nodes = scene.nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].shape.type == ShapeType::Line);
        CHECK(nodes[0].shape.line.to.x == 10.0f);
    }
}
