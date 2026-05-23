#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <atomic>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

// A mock node that is FrameInvariant — its params do not depend on frame.
// It stores a params_hash and a source_hash that are stable across frames.
class FrameInvariantMockNode final : public RenderGraphNode {
public:
    FrameInvariantMockNode(std::string name, u64 params_hash = 100, u64 source_hash = 200)
        : m_name(std::move(name)), m_params_hash(params_hash), m_source_hash(source_hash) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    bool cacheable() const override { return true; }

    CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mock_fi:" + m_name,
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = m_params_hash,
            .source_hash = m_source_hash
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, std::span<const std::shared_ptr<Framebuffer>>, std::span<const std::optional<raster::BBox>>) override {
        m_exec_count++;
        auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        fb->clear(Color::transparent());
        return fb;
    }

    int exec_count() const { return m_exec_count.load(); }

private:
    std::string m_name;
    u64 m_params_hash;
    u64 m_source_hash;
    mutable std::atomic<int> m_exec_count{0};
};

// A mock node that is FrameDependent (default).
class FrameDependentMockNode final : public RenderGraphNode {
public:
    FrameDependentMockNode(std::string name, u64 params_hash = 100)
        : m_name(std::move(name)), m_params_hash(params_hash) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mock_fd:" + m_name,
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = m_params_hash
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, std::span<const std::shared_ptr<Framebuffer>>, std::span<const std::optional<raster::BBox>>) override {
        m_exec_count++;
        auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        fb->clear(Color::transparent());
        return fb;
    }

    int exec_count() const { return m_exec_count.load(); }

private:
    std::string m_name;
    u64 m_params_hash;
    mutable std::atomic<int> m_exec_count{0};
};

} // namespace

// ── Test 1: Frame-invariant key ignores frame when node is not frame_dependent ──
TEST_CASE("frame_invariant node cache hit across frames") {
    cache::NodeCache node_cache;
    GraphExecutor executor;

    RenderGraph graph;
    auto node = std::make_unique<FrameInvariantMockNode>("Static", 42, 99);
    node->set_frame_dependent(false);
    auto id = graph.add_node(std::move(node));
    graph.set_output(id);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
    };

    // Frame 0: miss, executes
    ctx.frame = 0;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().hits == 0);
    CHECK(node_cache.stats().misses == 1);

    // Frame 1: should hit (frame=0 in key since !frame_dependent)
    ctx.frame = 1;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().hits == 1);
    CHECK(node_cache.stats().misses == 1);

    // Frame 2: should hit
    ctx.frame = 2;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().hits == 2);
    CHECK(node_cache.stats().misses == 1);
}

// ── Test 2: Frame-dependent key includes frame (default) ──
TEST_CASE("frame_dependent node cache miss on every frame") {
    cache::NodeCache node_cache;
    GraphExecutor executor;

    RenderGraph graph;
    auto node = std::make_unique<FrameDependentMockNode>("Animated", 42);
    // Default: frame_dependent() == true, cache_frame_policy() == FrameDependent
    auto id = graph.add_node(std::move(node));
    graph.set_output(id);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
    };

    // Frame 0: miss
    ctx.frame = 0;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().hits == 0);
    CHECK(node_cache.stats().misses == 1);

    // Frame 1: miss (different frame in key)
    ctx.frame = 1;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().hits == 0);
    CHECK(node_cache.stats().misses == 2);

    // Frame 2: miss
    ctx.frame = 2;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().hits == 0);
    CHECK(node_cache.stats().misses == 3);
}

// ── Test 3: Frame-invariant node with frame_dependent=false but changing params ──
TEST_CASE("frame_invariant node misses when params_hash changes") {
    cache::NodeCache node_cache;
    GraphExecutor executor;

    // First graph: params_hash=42
    RenderGraph graph1;
    auto node1 = std::make_unique<FrameInvariantMockNode>("Static", 42, 99);
    node1->set_frame_dependent(false);
    auto id1 = graph1.add_node(std::move(node1));
    graph1.set_output(id1);

    // Second graph: params_hash=99 (different)
    RenderGraph graph2;
    auto node2 = std::make_unique<FrameInvariantMockNode>("Static", 99, 99);
    node2->set_frame_dependent(false);
    auto id2 = graph2.add_node(std::move(node2));
    graph2.set_output(id2);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
    };

    // Graph1 frame 0: miss
    ctx.frame = 0;
    executor.execute(graph1, graph1.output(), ctx);
    CHECK(node_cache.stats().misses == 1);

    // Graph2 frame 0: miss (different params_hash)
    executor.execute(graph2, graph2.output(), ctx);
    CHECK(node_cache.stats().misses == 2);

    // Graph1 frame 1: should hit (key matches first graph's key)
    ctx.frame = 1;
    executor.execute(graph1, graph1.output(), ctx);
    CHECK(node_cache.stats().hits == 1);
}

// ── Test 4: Input invalidation propagates to downstream FrameInvariant node ──
TEST_CASE("frame_invariant node misses when input hash changes") {
    cache::NodeCache node_cache(1024ULL * 1024 * 1024);
    GraphExecutor executor;

    RenderGraph graph;
    // Source A: frame-dependent (produces different key per frame)
    auto src_a = std::make_unique<FrameDependentMockNode>("SrcA", 10);
    auto id_a = graph.add_node(std::move(src_a));

    // Composite B: FrameInvariant, depends on A
    auto comp_b = std::make_unique<CompositeNode>(BlendMode::Normal);
    comp_b->set_frame_dependent(false);
    auto id_b = graph.add_node(std::move(comp_b));
    graph.connect(id_a, id_b);
    graph.set_output(id_b);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
        .cache_enabled = true,
    };

    // Frame 0: A miss, B miss (input_hash from A included)
    ctx.frame = 0;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().misses == 2);
    CHECK(node_cache.stats().hits == 0);

    // Frame 1: A miss (frame-dependent), B miss (input_hash changed because A's digest changed)
    ctx.frame = 1;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().misses == 4);
    CHECK(node_cache.stats().hits == 0);
}

// ── Test 5: Input invalidation with static source → FrameInvariant composite hits ──
TEST_CASE("frame_invariant composite hits when static source is cached") {
    cache::NodeCache node_cache(1024ULL * 1024 * 1024);
    GraphExecutor executor;

    RenderGraph graph;
    // Source A: FrameInvariant, frame_dependent=false
    auto src_a = std::make_unique<FrameInvariantMockNode>("SrcA", 10, 99);
    src_a->set_frame_dependent(false);
    auto id_a = graph.add_node(std::move(src_a));

    // Composite B: FrameInvariant, frame_dependent=false
    auto comp_b = std::make_unique<CompositeNode>(BlendMode::Normal);
    comp_b->set_frame_dependent(false);
    auto id_b = graph.add_node(std::move(comp_b));
    graph.connect(id_a, id_b);
    graph.set_output(id_b);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
        .cache_enabled = true,
    };

    // Frame 0: A miss, B miss
    ctx.frame = 0;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().misses == 2);
    CHECK(node_cache.stats().hits == 0);

    // Frame 1: A hit (frame-invariant), B hit (same input_hash from A)
    ctx.frame = 1;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().misses == 2);
    CHECK(node_cache.stats().hits == 2);

    // Frame 2: both hit
    ctx.frame = 2;
    executor.execute(graph, graph.output(), ctx);
    CHECK(node_cache.stats().misses == 2);
    CHECK(node_cache.stats().hits == 4);
}

TEST_CASE("clear node is frame invariant") {
    ClearNode clear;
    CHECK(clear.cache_frame_policy() == CacheFramePolicy::FrameInvariant);
}

TEST_CASE("default render graph node is frame dependent") {
    // Cannot instantiate RenderGraphNode directly (abstract),
    // but we check the default via the FrameDependentMockNode which uses default.
    FrameDependentMockNode node("test");
    CHECK(node.cache_frame_policy() == CacheFramePolicy::FrameDependent);
}
