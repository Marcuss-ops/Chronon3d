#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/renderer/software/render_graph.hpp>
#include <mutex>

namespace chronon3d::graph {

static std::mutex g_exec_mutex;

GraphExecutor::GraphExecutor() {
    m_scheduler.Initialize();
}

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
    {
        std::lock_guard<std::mutex> lock(g_exec_mutex);
        if (auto it = m_temp.find(id); it != m_temp.end()) {
            return it->second;
        }
    }

    auto& node = graph.node(id);

    auto input_ids = graph.inputs(id);
    std::vector<std::shared_ptr<Framebuffer>> inputs(input_ids.size());
    
    if (!input_ids.empty()) {
        enki::TaskSet input_tasks(static_cast<uint32_t>(input_ids.size()), [&](enki::TaskSetPartition range, uint32_t threadnum) {
            for (auto i = range.start; i < range.end; ++i) {
                inputs[i] = execute_node(graph, input_ids[i], ctx);
            }
        });
        m_scheduler.AddTaskSetToPipe(&input_tasks);
        m_scheduler.WaitforTask(&input_tasks);
    }

    u64 input_hash = 0;
    for (size_t i = 0; i < input_ids.size(); ++i) {
        auto input_id = input_ids[i];
        
        std::lock_guard<std::mutex> lock(g_exec_mutex);
        auto digest_it = m_resolved_key_digest.find(input_id);
        u64 input_digest = (digest_it != m_resolved_key_digest.end())
            ? digest_it->second
            : graph.node(input_id).cache_key(ctx).digest();
        input_hash = render_graph::hash_combine(input_hash, input_digest);
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
            {
                std::lock_guard<std::mutex> lock(g_exec_mutex);
                m_resolved_key_digest[id] = key.digest();
                m_temp[id] = cached;
            }
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

    {
        std::lock_guard<std::mutex> lock(g_exec_mutex);
        m_resolved_key_digest[id] = key.digest();

        if (use_cache && result) {
            ctx.node_cache->store(key, result);
        }

        m_temp[id] = result;
    }
    return result;
}

} // namespace chronon3d::graph
