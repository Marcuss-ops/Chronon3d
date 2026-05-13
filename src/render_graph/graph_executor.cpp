#include <chronon3d/render_graph/graph_executor.hpp>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    m_temp.clear();
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
    for (auto input_id : graph.inputs(id)) {
        inputs.push_back(execute_node(graph, input_id, ctx));
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    auto key = node.cache_key(ctx);

    if (ctx.cache_enabled && ctx.node_cache) {
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

    if (ctx.cache_enabled && ctx.node_cache && result) {
        ctx.node_cache->store(key, result);
    }


    m_temp[id] = result;
    return result;
}

} // namespace chronon3d::graph
