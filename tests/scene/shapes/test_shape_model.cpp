#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
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

    SUBCASE("Text node has ShapeType::Text and maps TextSpec fields") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.layer("text-layer", [](LayerBuilder& l) {
                // PR3→PR4 migration: TextSpec is composable.  Top-level
                // `text` flat field replaced by `.content.value`; remaining
                // text-layout knobs live inside `.layout`.
                l.text("test-text", {
                    .content = {.value = "Hello"},
                    // Designators MUST appear in TextLayoutSpec declaration
                    // order: box, anchor, centering_mode, align, vertical_align,
                    // wrap, overflow, line_height, tracking, auto_fit,
                    // min_font_size, max_font_size, max_lines, ellipsis.
                    .layout  = {
                        .wrap          = TextWrap::Character,
                        .overflow      = TextOverflow::Ellipsis,
                        .auto_fit      = true,
                        .min_font_size = 14.0f,
                        .max_font_size = 120.0f,
                        .max_lines     = 4,
                        .ellipsis      = true,
                    },
                });
            });
            return s.build();
        });
        auto scene = comp.evaluate(0);
        const auto& layers = scene.layers();
        REQUIRE(layers.size() == 1);
        const auto& nodes = layers[0].nodes;
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].shape.type == ShapeType::Text);
        CHECK(nodes[0].shape.text.style.auto_fit == true);
        CHECK(nodes[0].shape.text.style.auto_scale == true);
        CHECK(nodes[0].shape.text.style.max_lines == 4);
        CHECK(nodes[0].shape.text.style.ellipsis == true);
        CHECK(nodes[0].shape.text.style.min_size == doctest::Approx(14.0f));
        CHECK(nodes[0].shape.text.style.max_size == doctest::Approx(120.0f));
        CHECK(nodes[0].shape.text.style.overflow == TextOverflow::Ellipsis);
        CHECK(nodes[0].shape.text.style.wrap == TextWrap::Character);
    }
}
