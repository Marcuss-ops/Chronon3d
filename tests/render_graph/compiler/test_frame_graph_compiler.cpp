#include <doctest/doctest.h>

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <memory>
#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

class CompilerTestNode final : public RenderGraphNode {
public:
    explicit CompilerTestNode(std::string n, bool cache = true, bool frame_dep = false)
        : m_name(std::move(n)), m_cacheable(cache) {
        set_frame_dependent(frame_dep);
    }

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }
    bool cacheable() const override { return m_cacheable; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>>
    ) const override {
        return raster::BBox{0, 0, ctx.frame.frame.width, ctx.frame.frame.height};
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override {
        return cache::NodeCacheKey{.scope = m_name, .frame = 0, .width = 0, .height = 0};
    }

    OwnedFB execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        return nullptr;
    }

private:
    std::string m_name;
    bool m_cacheable;
};

} // namespace

TEST_CASE("FrameGraphCompiler - handles empty graph") {
    RenderGraph graph;
    RenderGraphContext ctx;
    FrameGraphCompiler compiler;

    auto compiled = compiler.compile(std::move(graph), ctx);

    CHECK(compiled.empty());
}

TEST_CASE("FrameGraphCompiler - linear graph compilation") {
    RenderGraph graph;
    
    GraphNodeId clear_id = graph.add_node(std::make_unique<CompilerTestNode>("Clear"));
    GraphNodeId source_id = graph.add_node(std::make_unique<CompilerTestNode>("Source"));
    GraphNodeId composite_id = graph.add_node(std::make_unique<CompilerTestNode>("Composite"));

    graph.connect(clear_id, source_id);
    graph.connect(source_id, composite_id);
    graph.set_output(composite_id);

    RenderGraphContext ctx;
    ctx.frame.frame.width = 100;
    ctx.frame.frame.height = 100;
    
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    auto compiled = compiler.compile(std::move(graph), ctx, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.levels.size() >= 3);
    CHECK(compiled.consumer_counts[clear_id] == 1);
    CHECK(compiled.consumer_counts[source_id] == 1);
    CHECK(compiled.output == composite_id);
}

TEST_CASE("FrameGraphCompiler - diamond graph scheduling") {
    RenderGraph graph;

    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c = graph.add_node(std::make_unique<CompilerTestNode>("C"));
    GraphNodeId d = graph.add_node(std::make_unique<CompilerTestNode>("D"));

    graph.connect(a, b);
    graph.connect(a, c);
    graph.connect(b, d);
    graph.connect(c, d);
    graph.set_output(d);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    auto compiled = compiler.compile(std::move(graph), ctx, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.consumer_counts[a] == 2);
    CHECK(compiled.consumer_counts[b] == 1);
    CHECK(compiled.consumer_counts[c] == 1);
    CHECK(compiled.output == d);
}

TEST_CASE("FrameGraphCompiler - cycle detection throws") {
    RenderGraph graph;

    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));

    graph.connect(a, b);
    graph.connect(b, a);
    graph.set_output(b);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    CHECK_THROWS_AS(compiler.compile(std::move(graph), ctx, options), std::runtime_error);
}

TEST_CASE("FrameGraphCompiler - lifetimes computation") {
    RenderGraph graph;

    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c = graph.add_node(std::make_unique<CompilerTestNode>("C"));

    graph.connect(a, b);
    graph.connect(b, c);
    graph.set_output(c);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    auto compiled = compiler.compile(std::move(graph), ctx, options);

    REQUIRE(compiled.valid);
    
    // Lifetimes checks
    CHECK(compiled.lifetimes[a].producer == a);
    CHECK(compiled.lifetimes[b].producer == b);
    CHECK(compiled.lifetimes[a].last_level > compiled.lifetimes[a].first_level);
    CHECK(compiled.lifetimes[b].last_level > compiled.lifetimes[b].first_level);
}

TEST_CASE("FrameGraphCompiler - stable structure hash") {
    RenderGraph graph1;
    GraphNodeId a1 = graph1.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b1 = graph1.add_node(std::make_unique<CompilerTestNode>("B"));
    graph1.connect(a1, b1);
    graph1.set_output(b1);

    RenderGraph graph2;
    GraphNodeId a2 = graph2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = graph2.add_node(std::make_unique<CompilerTestNode>("B"));
    graph2.connect(a2, b2);
    graph2.set_output(b2);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    auto compiled1 = compiler.compile(std::move(graph1), ctx, options);
    auto compiled2 = compiler.compile(std::move(graph2), ctx, options);

    CHECK(compiled1.structure_hash == compiled2.structure_hash);
}
