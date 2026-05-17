#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

class TraceableMockNode : public RenderGraphNode {
public:
    TraceableMockNode(std::string name, std::atomic<int>& exec_count, std::vector<std::string>& exec_order, std::mutex& mutex, bool is_cacheable = true, u64 params_hash = 100)
        : m_name(std::move(name)), m_exec_count(exec_count), m_exec_order(exec_order), m_mutex(mutex), m_cacheable(is_cacheable), m_params_hash(params_hash) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }
    bool cacheable() const override { return m_cacheable; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mock:" + m_name,
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = m_params_hash
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>&) override {
        m_exec_count++;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_exec_order.push_back(m_name);
        }
        auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        fb->clear(Color::transparent());
        return fb;
    }

private:
    std::string m_name;
    std::atomic<int>& m_exec_count;
    std::vector<std::string>& m_exec_order;
    std::mutex& m_mutex;
    bool m_cacheable;
    u64 m_params_hash;
};

} // namespace

TEST_CASE("Test 7.1 — GraphExecutor executes and produces a valid output framebuffer") {
    std::atomic<int> exec_count{0};
    std::vector<std::string> exec_order;
    std::mutex mutex;

    RenderGraph graph;
    auto node_id = graph.add_node(std::make_unique<TraceableMockNode>("A", exec_count, exec_order, mutex));
    graph.set_output(node_id);

    RenderGraphContext ctx{.width = 80, .height = 60};
    GraphExecutor executor;
    auto res = executor.execute(graph, graph.output(), ctx);

    REQUIRE(res != nullptr);
    CHECK(res->width() == 80);
    CHECK(res->height() == 60);
    CHECK(exec_count == 1);
}

TEST_CASE("Test 7.2 — GraphExecutor esecuzione ricorsiva dell'ordine degli input") {
    std::atomic<int> exec_count{0};
    std::vector<std::string> exec_order;
    std::mutex mutex;

    RenderGraph graph;
    // Build tree: A -> B -> C (A is connected as input to B, B is connected as input to C)
    auto id_a = graph.add_node(std::make_unique<TraceableMockNode>("A", exec_count, exec_order, mutex));
    auto id_b = graph.add_node(std::make_unique<TraceableMockNode>("B", exec_count, exec_order, mutex));
    auto id_c = graph.add_node(std::make_unique<TraceableMockNode>("C", exec_count, exec_order, mutex));

    graph.connect(id_a, id_b);
    graph.connect(id_b, id_c);
    graph.set_output(id_c);

    RenderGraphContext ctx{.width = 16, .height = 16};
    GraphExecutor executor;
    executor.execute(graph, graph.output(), ctx);

    REQUIRE(exec_order.size() == 3);
    // Recursively, B must run after A, and C must run after B
    CHECK(exec_order[0] == "A");
    CHECK(exec_order[1] == "B");
    CHECK(exec_order[2] == "C");
}

TEST_CASE("Test 7.3 — GraphExecutor input condiviso eseguito una sola volta") {
    std::atomic<int> exec_count_a{0};
    std::atomic<int> exec_count_other{0};
    std::vector<std::string> exec_order;
    std::mutex mutex;

    RenderGraph graph;
    // Shared: A is input to both B and C. D takes B and C as inputs.
    auto id_a = graph.add_node(std::make_unique<TraceableMockNode>("A", exec_count_a, exec_order, mutex));
    auto id_b = graph.add_node(std::make_unique<TraceableMockNode>("B", exec_count_other, exec_order, mutex));
    auto id_c = graph.add_node(std::make_unique<TraceableMockNode>("C", exec_count_other, exec_order, mutex));
    auto id_d = graph.add_node(std::make_unique<TraceableMockNode>("D", exec_count_other, exec_order, mutex));

    graph.connect(id_a, id_b);
    graph.connect(id_a, id_c);
    graph.connect(id_b, id_d);
    graph.connect(id_c, id_d);
    graph.set_output(id_d);

    RenderGraphContext ctx{.width = 16, .height = 16};
    GraphExecutor executor;
    executor.execute(graph, graph.output(), ctx);

    // A must have been executed exactly once even though it's referenced twice in parallel
    CHECK(exec_count_a == 1);
}

TEST_CASE("Test 7.4 — GraphExecutor esecuzione con bypass cache / hit") {
    std::atomic<int> exec_count{0};
    std::vector<std::string> exec_order;
    std::mutex mutex;
    cache::NodeCache node_cache;

    RenderGraph graph;
    auto node_id = graph.add_node(std::make_unique<TraceableMockNode>("A", exec_count, exec_order, mutex));
    graph.set_output(node_id);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
    };

    GraphExecutor executor;

    // First execution: cache miss, runs once
    executor.execute(graph, graph.output(), ctx);
    CHECK(exec_count == 1);

    // Second execution with cache: cache hit, skips run
    executor.execute(graph, graph.output(), ctx);
    CHECK(exec_count == 1); // still 1

    // Third execution, clear cache: cache miss, runs again
    node_cache.clear();
    executor.execute(graph, graph.output(), ctx);
    CHECK(exec_count == 2);
}

TEST_CASE("Test 7.5 — GraphExecutor downstream node invalidation se cambia input") {
    std::atomic<int> exec_count_a{0};
    std::atomic<int> exec_count_b{0};
    std::vector<std::string> exec_order;
    std::mutex mutex;
    cache::NodeCache node_cache;

    RenderGraph graph;
    // B depends on A
    auto id_a = graph.add_node(std::make_unique<TraceableMockNode>("A", exec_count_a, exec_order, mutex, true, 100));
    auto id_b = graph.add_node(std::make_unique<TraceableMockNode>("B", exec_count_b, exec_order, mutex, true, 200));
    graph.connect(id_a, id_b);
    graph.set_output(id_b);

    RenderGraphContext ctx{
        .width = 32,
        .height = 32,
        .node_cache = &node_cache,
    };

    GraphExecutor executor;

    // 1st run
    executor.execute(graph, graph.output(), ctx);
    CHECK(exec_count_a == 1);
    CHECK(exec_count_b == 1);

    // 2nd run (cached)
    executor.execute(graph, graph.output(), ctx);
    CHECK(exec_count_a == 1);
    CHECK(exec_count_b == 1);

    // Create a new graph where A's params_hash has changed
    RenderGraph graph2;
    auto id_a2 = graph2.add_node(std::make_unique<TraceableMockNode>("A", exec_count_a, exec_order, mutex, true, 999)); // changed param
    auto id_b2 = graph2.add_node(std::make_unique<TraceableMockNode>("B", exec_count_b, exec_order, mutex, true, 200));
    graph2.connect(id_a2, id_b2);
    graph2.set_output(id_b2);

    // 3rd run: B's input key digest changed, invalidating B's cache, forcing both A and B to execute
    executor.execute(graph2, graph2.output(), ctx);
    CHECK(exec_count_a == 2);
    CHECK(exec_count_b == 2);
}
