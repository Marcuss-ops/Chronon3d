#include <doctest/doctest.h>

#include <chronon3d/rendering.hpp>

using namespace chronon3d;
using namespace chronon3d::rendergraph;

TEST_CASE("RenderGraph debug output includes nodes and edges") {
    RenderGraph graph;

    const RenderCacheKey canvas_key{
        .scope = "canvas",
        .frame = 12,
        .width = 1920,
        .height = 1080,
        .params_hash = 1,
        .source_hash = 2,
        .input_hash = 3,
    };

    auto canvas = graph.add_output("canvas", canvas_key);

    const RenderCacheKey transform_key{
        .scope = "root.transform",
        .frame = 12,
        .width = 1920,
        .height = 1080,
        .params_hash = 4,
        .source_hash = 5,
        .input_hash = canvas_key.digest(),
    };

    auto transform = graph.add_transform("root.transform", transform_key, canvas, Transform{});

    ::chronon3d::RenderNode node;
    node.name = "root";
    node.shape.type = ShapeType::Rect;
    node.shape.rect.size = {100.0f, 100.0f};
    node.color = Color::white();

    const RenderCacheKey source_key{
        .scope = "root.source",
        .frame = 12,
        .width = 1920,
        .height = 1080,
        .params_hash = 6,
        .source_hash = 7,
        .input_hash = transform_key.digest(),
    };

    auto source = graph.add_source("root.source", source_key, transform, node);

    const RenderCacheKey composite_key{
        .scope = "root.composite",
        .frame = 12,
        .width = 1920,
        .height = 1080,
        .params_hash = 8,
        .source_hash = 9,
        .input_hash = source_key.digest(),
    };

    graph.add_composite("root.composite", composite_key, canvas, source, BlendMode::Normal);

    const std::string dot = graph.debug_dot();
    CHECK(dot.find("canvas") != std::string::npos);
    CHECK(dot.find("root.transform") != std::string::npos);
    CHECK(dot.find("root.source") != std::string::npos);
    CHECK(dot.find("root.composite") != std::string::npos);
    CHECK(graph.size() == 4);
}

TEST_CASE("RenderCacheKey digest changes with parameters") {
    RenderCacheKey a{
        .scope = "scope",
        .frame = 1,
        .width = 64,
        .height = 64,
        .params_hash = 10,
        .source_hash = 20,
        .input_hash = 30,
    };
    RenderCacheKey b = a;
    b.params_hash = 11;

    CHECK(a.digest() != b.digest());
CHECK(a == a);
CHECK_FALSE(a == b);
}
