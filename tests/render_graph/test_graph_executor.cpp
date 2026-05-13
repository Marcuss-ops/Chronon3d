#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;
using namespace chronon3d::render_graph;

TEST_CASE("GraphExecutor caches final frames by key") {
    RenderGraph graph;
    const RenderCacheKey key{
        .scope = "canvas",
        .frame = 0,
        .width = 64,
        .height = 64,
        .params_hash = 1,
        .source_hash = 2,
        .input_hash = 3,
    };

    graph.add_output("canvas", key);

    SoftwareRenderer renderer;
    RenderGraphExecutionContext ctx{
        .renderer = renderer,
        .camera = Camera{},
        .frame = 0,
        .width = 64,
        .height = 64,
        .diagnostic = false,
    };

    GraphExecutor executor;
    auto first = executor.execute(graph, ctx, "demo", 10, 20);
    REQUIRE(first != nullptr);
    CHECK(first->width() == 64);
    CHECK(first->height() == 64);
    CHECK(executor.frame_cache().size() == 1);

    auto second = executor.execute(graph, ctx, "demo", 10, 20);
    REQUIRE(second != nullptr);
    CHECK(second->width() == 64);
    CHECK(second->height() == 64);
    CHECK(executor.frame_cache().size() == 1);
}

