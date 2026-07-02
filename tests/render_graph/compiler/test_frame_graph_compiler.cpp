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
    // PR2-cleanup: cache policy is decided at construction; legacy
    // `bool cache` / `bool frame_dep` ctor args and `m_cacheable` member were dropped.
    explicit CompilerTestNode(std::string n,
                               RenderNodeCachePolicy policy = static_memory_cache("test"))
        : RenderGraphNode(policy), m_name(std::move(n)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Source; }
    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>>
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override {
        return cache::NodeCacheKey{.scope = m_name, .frame = 0, .width = 0, .height = 0};
    }

    NodeExecResult execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        return NodeExecResult{};
    }

private:
    std::string m_name;
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
    ctx.frame_input.width = 100;
    ctx.frame_input.height = 100;
    
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

    CHECK_THROWS_AS(static_cast<void>(compiler.compile(std::move(graph), ctx, options)), std::runtime_error);
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

// ── TICKET-008 / §9.4 closure — `compile_with_reuse` reuse-path tests ──────────
// The line-~185 structure-hash canary above is extended with FIVE new
// TEST_CASEs (A-E below) per TICKET-008 Step 6.

TEST_CASE("FrameGraphCompiler - compile_with_reuse: skip path (Test A)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build prior via the standard compile() path.
    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    // Build a logically equivalent second graph for the reuse call.
    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    REQUIRE_FALSE(prior.levels.empty());
    REQUIRE(compiled.levels.size() == prior.levels.size());
    CHECK(compiled.levels == prior.levels);  // deep copy
    REQUIRE(compiled.nodes.size() == prior.nodes.size());
    for (size_t i = 0; i < compiled.nodes.size(); ++i) {
        CHECK(compiled.nodes[i].stable_node_id == prior.nodes[i].stable_node_id);
    }
    CHECK(compiled.consumer_counts == prior.consumer_counts);
    CHECK(compiled.output == prior.output);
    // structure_hash should be freshly derived and equal (hash is deterministic).
    CHECK(compiled.structure_hash == prior.structure_hash);
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: mismatched hash falls through (Test B)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;  // will be ignored by fall-through
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build prior: 2-node graph.
    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    // Build second graph with an extra node — structure differs from prior.
    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c2 = g2.add_node(std::make_unique<CompilerTestNode>("C"));
    g2.connect(a2, b2);
    g2.connect(b2, c2);
    g2.set_output(c2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    // Full path ran — levels/nodes are NOT bit-equal to prior's.
    CHECK(compiled.levels != prior.levels);
    CHECK(compiled.nodes.size() != prior.nodes.size());
    // Output reflects the new graph.
    CHECK(compiled.output == c2);
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: graph_structure_unchanged=false falls through (Test C)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = false;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.levels != prior.levels);  // predicate failed → fresh full path
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: run_optimizer=true falls through (Test D)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;
    FrameGraphCompileOptions options;
    options.run_optimizer = true;  // unsafe predicate: skip is gated off
    options.validate_dag = true;
    options.compute_lifetimes = true;

    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.levels != prior.levels);  // predicate gated by reuse_if_unchanged_predicate_safe()
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: post-conditions hold (Test E)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;
    ctx.node_exec.early_exit_skip.clear();  // populated below
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build prior
    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    // Build second graph; set early_exit_skip on ctx.node_exec for post-cond
    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);
    ctx.node_exec.early_exit_skip.assign(2, false);
    ctx.node_exec.early_exit_skip[0] = true;  // mark a2 skip

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    // structure_hash freshly derived equals compute_structure_hash on compiled.graph
    CHECK(compiled.structure_hash ==
          FrameGraphCompiler::compute_structure_hash(compiled.graph, compiled.output));
    // early_exit_skip propagated from ctx (per-node, size == graph.size())
    REQUIRE(compiled.early_exit_skip.size() == compiled.graph.size());
    CHECK(compiled.early_exit_skip[0] == true);
    CHECK(compiled.early_exit_skip[1] == false);
    // graph_instance_id was freshly derived (defensive even on skip path)
    CHECK(compiled.graph_instance_id.value != kInvalidGraphInstanceId.value);
    // skip_initial_clear copied from policy
    CHECK(compiled.skip_initial_clear == ctx.policy.skip_initial_clear);
}

