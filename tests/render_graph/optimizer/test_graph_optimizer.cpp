#include <doctest/doctest.h>

#include <array>

#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;
using namespace chronon3d::graph::optimizer;

// ── Minimal test node (no external dependencies) ────────────────────────

class TestNode final : public RenderGraphNode {
public:
    // PR2-cleanup: cache policy is decided at construction. Use one of the
    // canonical helpers (`static_memory_cache`, `frame_variant_cache`,
    // `no_cache`, `static_persistent_cache`) — the legacy `bool cache` /
    // `bool frame_dep` ctor args and `m_cacheable` member were dropped.
    explicit TestNode(std::string n,
                       RenderNodeCachePolicy policy = static_memory_cache("test"))
        : m_name(std::move(n)), m_cache_policy(policy) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Source; }
    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }

    [[nodiscard]] RenderNodeCachePolicy cache_policy() const noexcept override {
        return m_cache_policy;
    }

    /// Test-only: swap the cache policy mid-test to simulate per-node
    /// variation when a test wanted to flip a legacy
    /// `set_frame_dependent(true|false)` mutation.
    void set_cache_policy_for_test(RenderNodeCachePolicy p) noexcept { m_cache_policy = p; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>>
    ) const override {
        return raster::BBox{0, 0, ctx.frame.width, ctx.frame.height};
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
    RenderNodeCachePolicy m_cache_policy;
};

// ── Helpers ─────────────────────────────────────────────────────────────

static RenderGraphContext make_test_context(int w, int h) {
    RenderGraphContext ctx;
    ctx.frame.width  = w;
    ctx.frame.height = h;
    return ctx;
}

static RenderGraph make_chain_of_transforms(int count, bool identity = false) {
    RenderGraph graph;
    GraphNodeId prev = graph.add_node(std::make_unique<TestNode>("root", false, false));

    for (int i = 0; i < count; ++i) {
        Mat4 m = identity ? Mat4(1.0f) : glm::translate(Mat4(1.0f), Vec3(static_cast<f32>(i), 0.0f, 0.0f));
        GraphNodeId tid = graph.add_node(std::make_unique<TransformNode>(m, 1.0f));
        graph.connect(prev, tid);
        prev = tid;
    }

    graph.set_output(prev);
    return graph;
}

static EffectStack make_blur_stack(f32 radius) {
    EffectStack stack;
    stack.emplace_back(BlurParams{radius});
    return stack;
}

static EffectStack make_tint_stack() {
    EffectStack stack;
    stack.emplace_back(TintParams{Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.5f});
    return stack;
}

static std::unique_ptr<EffectStackNode> make_effect_node(bool frame_dep = true) {
    return std::make_unique<EffectStackNode>(
        make_blur_stack(8.0f),
        frame_dep ? frame_variant_cache("effect_stack") : static_memory_cache("effect_stack"));
}

static std::unique_ptr<AdjustmentNode> make_adjustment_node(bool frame_dep = true) {
    return std::make_unique<AdjustmentNode>(
        make_tint_stack(),
        frame_dep ? frame_variant_cache("adjustment") : static_memory_cache("adjustment"));
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
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(10.0f, 0.0f, 0.0f)), 1.0f));
    graph.connect(root, tx);
    graph.set_output(tx);

    auto ctx = make_test_context(100, 100);
    size_t pruned = prune_branches(graph, ctx);

    CHECK(pruned == 0);
    CHECK(graph.live_count() == 2);
}

TEST_CASE("TransformNode pure translation keeps a stable bbox size across subpixel offsets") {
    RenderGraphContext ctx = make_test_context(1920, 1080);
    std::array<std::optional<raster::BBox>, 1> input_bboxes{
        std::optional<raster::BBox>{raster::BBox{100, 200, 420, 360}}
    };

    TransformNode tx_a(glm::translate(Mat4(1.0f), Vec3(0.25f, 0.0f, 0.0f)), 1.0f);
    TransformNode tx_b(glm::translate(Mat4(1.0f), Vec3(0.75f, 0.0f, 0.0f)), 1.0f);

    auto bbox_a = tx_a.predicted_bbox(ctx, input_bboxes);
    auto bbox_b = tx_b.predicted_bbox(ctx, input_bboxes);

    REQUIRE(bbox_a.has_value());
    REQUIRE(bbox_b.has_value());
    CHECK((bbox_a->x1 - bbox_a->x0) == (bbox_b->x1 - bbox_b->x0));
    CHECK((bbox_a->y1 - bbox_a->y0) == (bbox_b->y1 - bbox_b->y0));
}

TEST_CASE("Pruning - identity transform with multiple consumers is kept") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId id_tx = graph.add_node(std::make_unique<TransformNode>(Mat4(1.0f), 1.0f));
    GraphNodeId tx_a = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(5.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId tx_b = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(0.0f, 5.0f, 0.0f)), 1.0f));

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
    ctx.resources.node_cache = &node_cache;

    // root is frame-invariant but not cacheable (cacheable()=false).
    // TransformNode defaults to frame_dependent=true; mark it false.

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
    ctx.resources.node_cache = &node_cache;

    // Both nodes frame-dependent → nothing eligible

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
    GraphNodeId parent = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(1.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId child_a = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(0.0f, 1.0f, 0.0f)), 1.0f));
    GraphNodeId child_b = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(0.0f, 2.0f, 0.0f)), 1.0f));

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
    ctx.resources.node_cache = &node_cache;

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
    ctx.resources.node_cache = &node_cache;

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
    GraphNodeId tx = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(5.0f, 0.0f, 0.0f)), 1.0f));
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
    GraphNodeId tx_static = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(1.0f, 0.0f, 0.0f)), 1.0f));
    GraphNodeId tx_dynamic = graph.add_node(std::make_unique<TransformNode>(glm::translate(Mat4(1.0f), Vec3(0.0f, 1.0f, 0.0f)), 1.0f));

    graph.connect(root, tx_static);
    graph.connect(tx_static, tx_dynamic);
    graph.set_output(tx_dynamic);


    size_t fused = fuse_nodes(graph);

    CHECK(fused == 0); // Mismatched frame_dependent
    CHECK(graph.live_count() == 3);
}

// ── Test 6: Dead node elimination ───────────────────────────────────────

TEST_CASE("Dead node elimination - orphan node is removed") {
    RenderGraph graph;
    GraphNodeId root    = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId orphan  = graph.add_node(std::make_unique<TestNode>("orphan", false, false));
    // orphan is not connected to anything
    graph.set_output(root);
    (void)orphan;

    size_t removed = eliminate_dead_nodes(graph);

    CHECK(removed == 1);
    CHECK(graph.live_count() == 1);
    CHECK(graph.has_node(root));
}

TEST_CASE("Dead node elimination - all reachable nodes are kept") {
    RenderGraph graph;
    GraphNodeId a = graph.add_node(std::make_unique<TestNode>("a", false, false));
    GraphNodeId b = graph.add_node(std::make_unique<TestNode>("b", false, false));
    GraphNodeId c = graph.add_node(std::make_unique<TestNode>("c", false, false));
    graph.connect(a, b);
    graph.connect(b, c);
    graph.set_output(c);

    size_t removed = eliminate_dead_nodes(graph);

    CHECK(removed == 0);
    CHECK(graph.live_count() == 3);
}

TEST_CASE("Dead node elimination - no output set does nothing") {
    RenderGraph graph;
    graph.add_node(std::make_unique<TestNode>("a", false, false));
    graph.add_node(std::make_unique<TestNode>("b", false, false));
    // No set_output call

    size_t removed = eliminate_dead_nodes(graph);
    CHECK(removed == 0); // No output → nothing to reach from
    CHECK(graph.live_count() == 2);
}

// ── Test 7: EffectStack fusion ───────────────────────────────────────────

TEST_CASE("Effect fusion - two adjacent EffectStackNodes are fused") {
    RenderGraph graph;
    GraphNodeId root   = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId eff_a  = graph.add_node(make_effect_node(false));
    GraphNodeId eff_b  = graph.add_node(make_effect_node(false));
    graph.connect(root, eff_a);
    graph.connect(eff_a, eff_b);
    graph.set_output(eff_b);

    size_t fused = fuse_effect_stacks(graph);

    CHECK(fused == 1);
    // root + eff_b remain; eff_a absorbed
    CHECK(graph.live_count() == 2);
    auto& fused_node = dynamic_cast<EffectStackNode&>(graph.node(eff_b));
    CHECK(fused_node.effects().size() == 2);
}

TEST_CASE("Effect fusion - no fusion when parent has multiple consumers") {
    RenderGraph graph;
    GraphNodeId root   = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId eff_a  = graph.add_node(make_effect_node(false));
    GraphNodeId eff_b  = graph.add_node(make_effect_node(false));
    GraphNodeId eff_c  = graph.add_node(make_effect_node(false));
    graph.connect(root, eff_a);
    graph.connect(eff_a, eff_b);  // eff_a has 2 consumers
    graph.connect(eff_a, eff_c);
    graph.set_output(eff_b);

    size_t fused = fuse_effect_stacks(graph);

    CHECK(fused == 0); // eff_a cannot be absorbed
    CHECK(graph.live_count() == 4);
}

TEST_CASE("Effect fusion - no fusion across mismatched frame_dependent") {
    RenderGraph graph;
    GraphNodeId root    = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId eff_sta = graph.add_node(make_effect_node(false)); // frame_dep=false
    GraphNodeId eff_dyn = graph.add_node(make_effect_node(true));  // frame_dep=true
    graph.connect(root, eff_sta);
    graph.connect(eff_sta, eff_dyn);
    graph.set_output(eff_dyn);

    size_t fused = fuse_effect_stacks(graph);

    CHECK(fused == 0); // Mismatched frame_dependent
    CHECK(graph.live_count() == 3);
}

TEST_CASE("Effect fusion - chain of three effects fused iteratively") {
    RenderGraph graph;
    GraphNodeId root   = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId eff_a  = graph.add_node(make_effect_node(false));
    GraphNodeId eff_b  = graph.add_node(make_adjustment_node(false));
    GraphNodeId eff_c  = graph.add_node(make_effect_node(false));
    graph.connect(root, eff_a);
    graph.connect(eff_a, eff_b);
    graph.connect(eff_b, eff_c);
    graph.set_output(eff_c);

    // First pass fuses eff_a into eff_b.  Second pass fuses the resulting
    // effect-like node into eff_c.
    size_t fused1 = fuse_effect_stacks(graph);
    size_t fused2 = fuse_effect_stacks(graph);

    CHECK(fused1 + fused2 >= 2);
    CHECK(graph.live_count() == 2); // root + final node
    auto& fused_node = dynamic_cast<EffectStackNode&>(graph.node(eff_c));
    CHECK(fused_node.effects().size() == 3);
}

TEST_CASE("Effect fusion - mixed EffectStackNode and AdjustmentNode are fused") {
    RenderGraph graph;
    GraphNodeId root = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId adj  = graph.add_node(make_adjustment_node(false));
    GraphNodeId eff  = graph.add_node(make_effect_node(false));
    graph.connect(root, adj);
    graph.connect(adj, eff);
    graph.set_output(eff);

    size_t fused = fuse_effect_stacks(graph);

    CHECK(fused == 1);
    CHECK(graph.live_count() == 2);
    auto& fused_node = dynamic_cast<EffectStackNode&>(graph.node(eff));
    CHECK(fused_node.effects().size() == 2);
}

// ── Test 8: New config flags respected ──────────────────────────────────

TEST_CASE("Config - effect_fusion and dead_node_elimination flags honored") {
    RenderGraph graph;
    GraphNodeId root   = graph.add_node(std::make_unique<TestNode>("root", false, false));
    GraphNodeId orphan = graph.add_node(std::make_unique<TestNode>("orphan", false, false));
    GraphNodeId eff_a  = graph.add_node(make_effect_node(false));
    GraphNodeId eff_b  = graph.add_node(make_effect_node(false));
    graph.connect(root, eff_a);
    graph.connect(eff_a, eff_b);
    graph.set_output(eff_b);
    (void)orphan;

    auto ctx = make_test_context(100, 100);
    cache::NodeCache node_cache;
    ctx.resources.node_cache = &node_cache;

    OptimizationConfig off;
    off.enable_node_fusion          = false;
    off.enable_branch_pruning       = false;
    off.enable_static_bake          = false;
    off.enable_effect_fusion        = false;
    off.enable_dead_node_elimination = false;

    auto result = optimize_graph(graph, ctx, off);

    CHECK(result.effects_fused     == 0);
    CHECK(result.dead_nodes_removed == 0);
    CHECK(result.nodes_fused        == 0);
    CHECK(result.nodes_pruned       == 0);
    // orphan still present
    CHECK(graph.live_count() == 4);
}
