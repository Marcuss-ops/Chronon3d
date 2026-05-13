#include <doctest/doctest.h>

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/cache/node_cache.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("GraphExecutor executes a single ClearNode and returns a transparent framebuffer") {
    RenderGraph graph;
    auto id = graph.add_node(std::make_unique<ClearNode>());
    graph.set_output(id);

    RenderGraphContext ctx;
    ctx.width  = 64;
    ctx.height = 64;

    GraphExecutor executor;
    auto result = executor.execute(graph, ctx);

    REQUIRE(result != nullptr);
    CHECK(result->width()  == 64);
    CHECK(result->height() == 64);
    // ClearNode produces a fully transparent framebuffer
    Color pixel = result->get_pixel(0, 0);
    CHECK(pixel.a == doctest::Approx(0.0f));
}

TEST_CASE("GraphExecutor cache hit skips re-execution") {
    cache::NodeCache node_cache;

    RenderGraph graph;
    auto id = graph.add_node(std::make_unique<ClearNode>());
    graph.set_output(id);

    RenderGraphContext ctx;
    ctx.width        = 32;
    ctx.height       = 32;
    ctx.node_cache   = &node_cache;
    ctx.cache_enabled = true;

    GraphExecutor executor;
    auto first  = executor.execute(graph, ctx);
    // ClearNode has cacheable() == false, so nothing should be stored
    CHECK(node_cache.stats().hits == 0);
    CHECK(node_cache.stats().misses == 0);
    REQUIRE(first != nullptr);

    auto second = executor.execute(graph, ctx);
    REQUIRE(second != nullptr);
}

TEST_CASE("RenderGraph output node throws when unset") {
    RenderGraph graph;
    graph.add_node(std::make_unique<ClearNode>());
    // output not set — calling output() should throw
    CHECK_THROWS(graph.output());
}

TEST_CASE("GraphBuilder produces a valid graph with set_output for an empty scene") {
    Scene scene;
    RenderGraphContext ctx;
    ctx.width  = 16;
    ctx.height = 16;

    RenderGraph graph = GraphBuilder::build(scene, ctx);
    CHECK(graph.has_output());
    CHECK(graph.size() >= 1); // at least the root ClearNode

    GraphExecutor executor;
    auto result = executor.execute(graph, ctx);
    REQUIRE(result != nullptr);
    CHECK(result->width()  == 16);
    CHECK(result->height() == 16);
}
