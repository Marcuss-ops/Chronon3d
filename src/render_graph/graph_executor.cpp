#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <iostream>
#include <future>
#include <mutex>

namespace chronon3d::graph {

GraphExecutor::GraphExecutor() {}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    // ZoneScoped;
    return execute_node(graph, output, ctx);
}

std::shared_ptr<Framebuffer> GraphExecutor::execute_node(
    RenderGraph& graph,
    GraphNodeId id,
    RenderGraphContext& ctx
) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_temp.find(id); it != m_temp.end()) {
            return it->second;
        }
    }

    if (id >= graph.size()) return nullptr;
    auto& node = graph.node(id);

    const auto& input_ids = graph.inputs(id);
    std::vector<std::shared_ptr<Framebuffer>> inputs(input_ids.size());

    if (!input_ids.empty()) {
        std::vector<std::future<void>> futures;
        futures.reserve(input_ids.size());
        for (size_t i = 0; i < input_ids.size(); ++i) {
            futures.emplace_back(std::async(std::launch::async, [&, i] {
                inputs[i] = execute_node(graph, input_ids[i], ctx);
            }));
        }
        for (auto& f : futures) {
            f.get();
        }
    }

    std::shared_ptr<Framebuffer> result;
    const bool is_cacheable = node.cacheable();
    cache::NodeCacheKey key;

    if (is_cacheable && ctx.node_cache) {
        key = node.cache_key(ctx);
        
        // Combine input hashes to ensure downstream invalidation
        u64 input_hash = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto input_id : input_ids) {
                input_hash = hash_combine(input_hash, m_resolved_key_digest[input_id]);
            }
        }
        key.input_hash = input_hash;

        result = ctx.node_cache->find(key);
    }

    if (!result) {
        result = node.execute(ctx, inputs);
        if (is_cacheable && ctx.node_cache && result) {
            ctx.node_cache->store(key, result);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_temp[id] = result;
        m_resolved_key_digest[id] = key.digest();
    }

    return result;
}

} // namespace chronon3d::graph
