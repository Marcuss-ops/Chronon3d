#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/renderer/render_graph.hpp>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    m_temp.clear();
    m_resolved_key_digest.clear();
    return execute_node(graph, output, ctx);
}

std::shared_ptr<Framebuffer> GraphExecutor::execute_node(
    RenderGraph& graph,
    GraphNodeId id,
    RenderGraphContext& ctx
) {
    if (auto it = m_temp.find(id); it != m_temp.end()) {
        return it->second;
    }

    auto& node = graph.node(id);

    std::vector<std::shared_ptr<Framebuffer>> inputs;
    u64 input_hash = 0;
    for (auto input_id : graph.inputs(id)) {
        auto fb = execute_node(graph, input_id, ctx);
        inputs.push_back(fb);
        
        // Use the resolved digest (post input_hash injection) if available, so the
        // hash chain is fully recursive. Falls back to the raw cache_key digest on
        // the first pass (should not happen in a DAG traversal without cycles).
        auto digest_it = m_resolved_key_digest.find(input_id);
        u64 input_digest = (digest_it != m_resolved_key_digest.end())
            ? digest_it->second
            : graph.node(input_id).cache_key(ctx).digest();
        input_hash = rendergraph::hash_combine(input_hash, input_digest);
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    auto key = node.cache_key(ctx);
    key.input_hash = input_hash;

    const bool use_cache = node.cacheable() && ctx.cache_enabled && ctx.node_cache;

    if (use_cache) {
        if (auto cached = ctx.node_cache->find(key)) {
            if (ctx.profiler) {
                ctx.profiler->record_node({
                    .name = node.name(),
                    .kind = node.kind(),
                    .execution_time_ms = 0.0,
                    .cache_hit = true,
                    .memory_bytes = cached->size_bytes()
                });
            }
            m_resolved_key_digest[id] = key.digest();
            m_temp[id] = cached;
            return cached;
        }
    }

    auto result = node.execute(ctx, inputs);
    auto end_time = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (ctx.profiler) {
        ctx.profiler->record_node({
            .name = node.name(),
            .kind = node.kind(),
            .execution_time_ms = duration,
            .cache_hit = false,
            .memory_bytes = result ? result->size_bytes() : 0
        });
    }

    m_resolved_key_digest[id] = key.digest();

    if (use_cache && result) {
        ctx.node_cache->store(key, result);
    }

    m_temp[id] = result;
    return result;
}

} // namespace chronon3d::graph
