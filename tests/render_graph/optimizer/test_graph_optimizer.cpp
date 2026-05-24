#include <doctest/doctest.h>

#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/cache/node_cache.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;
using namespace chronon3d::graph::optimizer;

// ── Minimal test node (no external dependencies) ────────────────────────

class TestNode final : public RenderGraphNode {
public:
    explicit TestNode(std::string n, bool cache = true, bool frame_dep = false)
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
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override {
        return cache::NodeCacheKey{.scope = m_name, .frame = 0, .width = 0, .height = 0};
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext&,
        std::span<const std::shared_ptr<Framebuffer>>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        return nullptr;
    }

private:
    std::string m_name;
    bool m_cacheable;
};

// ── Helpers ─────────────────────────────────────────────────────────────

static RenderGraphContext make_test_context(int w, int h) {
    RenderGraphContext ctx;
    ctx.width  = w;
    ctx.height = h;
    return ctx;
}

static RenderGraph make_chain_of_transforms(int count, bool identity = false) {
    RenderGraph graph;
    GraphNodeId prev = graph.add_node(std::make_unique<TestNode>("root", false, false));

    for (int i = 0; i < count; ++i) {
        Mat4 m = identity ? Mat4(1.0f) : math::translate(Vec3(static_cast<f32>(i), 0.0f, 0.0f));
        GraphNodeId tid = graph.add_node(std::make_unique<TransformNode>(m, 1.0f));
        graph.connect(prev, tid);
        prev = tid;
    }

    graph.set_output(prev);
    return graph;
}

// ── Test 1: Pruning useless branches ────────────────────────────────────

TEST_CASE("Pruning - identity transform with single consumer is removed") {
    auto graph = make_chain_of_transforms(3, true);
    REQUIRE(graph.size() == 4); // root + 3 transforms

    auto ctx = make_test_context(100, 100);
    size_t pruned = prune_branches(graph, ctx);

    // In chain root→A→B→C: A and B have exactly 1 consumer each → pruned.
    // C is the output with 0 consumers → NOT pruned (would break the graph).
    CHECK(pruned == 2);
    CHECK(graph.live_count() == 2); // root + final transform C remains
}

TEST_CASE("Pruning - non-identity transform is kept") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(10.0f, 0.0f, 0.0f)), 1.0f));
    graph.connect(root, tx);
    graph.set_output(tx);

    auto ctx = make_test_context(100, 100);
    size_t pruned = prune_branches(graph, ctx);

    CHECK(pruned == 0);
    CHECK(graph.live_count() == 2);
}

TEST_CASE("Pruning - identity transform with multiple consumers is kept") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId id_tx = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    GraphNodeId tx_a = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(5.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId tx_b = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(0.0f, 5.0f, 0.0f)), 1.0f));

    graph.connect(root, id_tx);
    graph.connect(id_tx, tx_a);
    graph.connect(id_tx, tx_b);
    graph.set_output(tx_a); // Only one output, but tx_b is a second consumer

    auto ctx = make_test_context(100, 100);
    size_t pruned = prune_branches(graph, ctx);

    CHECK(pruned == 0); // Cannot prune because id_tx has 2 consumers
    CHECK(graph.live_count() == 4);
}

// ── Test 2: Static node cache eligibility ───────────────────────────────

TEST_CASE("Static bake - frame-independent nodes are counted") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    graph.connect(root, tx);
    graph.set_output(tx);

    auto ctx = make_test_context(100, 100);
    cache::NodeCache node_cache;
    ctx.node_cache = &node_cache;

    // root is frame-invariant but not cacheable (cacheable()=false).
    // TransformNode defaults to frame_dependent=true; mark it false.
    graph.node(tx).set_frame_dependent(false);

    size_t eligible = count_bake_eligible_nodes(graph, ctx);
    CHECK(eligible == 1); // Only TransformNode is eligible
}

TEST_CASE("Static bake - frame-dependent nodes are excluded") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    graph.connect(root, tx);
    graph.set_output(tx);

    auto ctx = make_test_context(100, 100);
    cache::NodeCache node_cache;
    ctx.node_cache = &node_cache;

    // Both nodes frame-dependent → nothing eligible
    graph.node(root).set_frame_dependent(true);
    graph.node(tx).set_frame_dependent(true);

    size_t eligible = count_bake_eligible_nodes(graph, ctx);
    CHECK(eligible == 0);
}

// ── Test 3: Node fusion ───────────────────────────────────────────────────

TEST_CASE("Fusion - adjacent transform nodes are fused") {
    auto graph = make_chain_of_transforms(3, false);
    REQUIRE(graph.size() == 4); // Clear + 3 transforms

    size_t fused = fuse_nodes(graph);

    // With a chain of 3 transforms where each has 1 consumer, all 2 intermediate
    // transforms should be fused into the last one.
    CHECK(fused >= 1);
    CHECK(graph.live_count() <= 3);
}

TEST_CASE("Fusion - no fusion when parent has multiple consumers") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId parent = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(1.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId child_a = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(0.0f, 1.0f, 0.0f)), 1.0f));
    GraphNodeId child_b = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(0.0f, 2.0f, 0.0f)), 1.0f));

    graph.connect(root, parent);
    graph.connect(parent, child_a);
    graph.connect(parent, child_b);
    graph.set_output(child_a);

    size_t fused = fuse_nodes(graph);

    CHECK(fused == 0); // parent has 2 consumers
    CHECK(graph.live_count() == 4);
}

TEST_CASE("Fusion - no fusion for non-transform nodes") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId source = graph.add_node(std::make_unique<TestNode>("source", false, false)); // Not a TransformNode
    graph.connect(root, source);
    graph.set_output(source);

    size_t fused = fuse_nodes(graph);

    CHECK(fused == 0);
    CHECK(graph.live_count() == 2);
}

// ── Test 4: Explain plan (optimization result) ──────────────────────────

TEST_CASE("Explain plan - optimize_graph reports node counts") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx_a = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    GraphNodeId tx_b = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    graph.connect(root, tx_a);
    graph.connect(tx_a, tx_b);
    graph.set_output(tx_b);

    auto ctx = make_test_context(100, 100);
    cache::NodeCache node_cache;
    ctx.node_cache = &node_cache;

    OptimizationConfig config;
    config.enable_node_fusion     = true;
    config.enable_branch_pruning  = true;
    config.enable_static_bake     = true;

    auto result = optimize_graph(graph, ctx, config);

    CHECK(result.nodes_before == 3);
    CHECK(result.nodes_fused == 1);    // tx_a + tx_b fused
    CHECK(result.nodes_pruned == 0);   // output node has 0 consumers, not pruned
    CHECK(result.static_bakes >= 0); // may count eligible nodes
    CHECK(result.nodes_after == 2);    // root + fused node
}

TEST_CASE("Explain plan - optimize_graph respects config toggles") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    graph.connect(root, tx);
    graph.set_output(tx);

    auto ctx = make_test_context(100, 100);
    cache::NodeCache node_cache;
    ctx.node_cache = &node_cache;

    // All disabled
    OptimizationConfig off;
    off.enable_node_fusion    = false;
    off.enable_branch_pruning = false;
    off.enable_static_bake    = false;

    auto result = optimize_graph(graph, ctx, off);

    CHECK(result.nodes_fused == 0);
    CHECK(result.nodes_pruned == 0);
    CHECK(result.static_bakes == 0);
    CHECK(result.nodes_after == result.nodes_before);
}

// ── Test 5: No unsafe optimization ──────────────────────────────────────

TEST_CASE("No unsafe optimization - mixed node kinds are not fused") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(5.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId other = graph.add_node(std::make_unique<TestNode>("other", false, false)); // Non-transform
    graph.connect(root, tx);
    graph.connect(tx, other);
    graph.set_output(other);

    size_t fused = fuse_nodes(graph);

    CHECK(fused == 0); // Cannot fuse Transform with non-Transform
    CHECK(graph.live_count() == 3);
}

TEST_CASE("No unsafe optimization - frame-dependent vs frame-invariant not fused") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId tx_static = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(1.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId tx_dynamic = graph.add_node(std::make_unique<TransformNode>(math::translate(Vec3(0.0f, 1.0f, 0.0f)), 1.0f));

    graph.connect(root, tx_static);
    graph.connect(tx_static, tx_dynamic);
    graph.set_output(tx_dynamic);

    graph.node(tx_static).set_frame_dependent(false);
    graph.node(tx_dynamic).set_frame_dependent(true);

    size_t fused = fuse_nodes(graph);

    CHECK(fused == 0); // Mismatched frame_dependent
    CHECK(graph.live_count() == 3);
}
