#include "graph_executor_internal.hpp"
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/core/profiling.hpp>
#include <spdlog/spdlog.h>
#include <tbb/tbb.h>

namespace chronon3d::graph {

void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderTrace* parent_trace,
    int32_t parent_frame,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool
) {
    profiling::g_current_trace = parent_trace;
    profiling::g_current_frame = parent_frame;
    profiling::g_current_counters = parent_counters;
    profiling::g_current_framebuffer_pool = parent_pool;

    auto& node = graph.node(id);
    const auto& input_ids = graph.inputs(id);
    const auto& pr = level_resolved[level_index];

    // Cache evaluation
    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id,
        pr.inputs_all_cache_hits
    );

    // Predicted bbox
    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);

    // Dirty-rect clipping
    RenderGraphContext node_ctx = ctx;
    if (ctx.dirty_rects_enabled) {
        node_ctx.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    } else {
        node_ctx.clip_rect = predicted_bbox;
    }

    // Node execution
    const double duration_ms = run_node(
        node, node_ctx,
        pr.inputs, pr.input_bboxes,
        cache_eval.use_cache,
        cache_eval.key,
        cache_eval.result,
        ctx
    );

    // Telemetry
    emit_node_records(
        ctx, node,
        cache_eval.key,
        cache_eval.result,
        node_ctx.clip_rect,
        cache_eval.cache_status,
        cache_eval.is_cacheable,
        static_cast<int>(input_ids.size()),
        duration_ms
    );

    // Update shared state
    state.temp[id] = cache_eval.result;
    state.resolved_key_digest[id] = cache_eval.key.digest();
    state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
    state.resolved_cache_hit[id] = (cache_eval.cache_status == "hit") ? 1 : 0;
    state.resolved_bboxes[id] = predicted_bbox;

    // Restore worker thread-local hygiene
    profiling::g_current_trace = nullptr;
    profiling::g_current_counters = nullptr;
    profiling::g_current_framebuffer_pool = nullptr;
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    const auto plan = build_execution_plan(graph, output);
    if (plan.levels.empty()) {
        return nullptr;
    }

    auto* res = m_frame_arena.resource();
    struct ArenaGuard { 
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{m_frame_arena};

    return m_arena.execute([&]() -> std::shared_ptr<Framebuffer> {
        const size_t node_count = graph.size();
        ExecutionState state(res);
        state.temp.resize(node_count);
        state.resolved_key_digest.assign(node_count, 0);
        state.resolved_frame_dependent.assign(node_count, 0);
        state.resolved_cache_hit.assign(node_count, 0);
        state.resolved_bboxes.resize(node_count);

        std::pmr::vector<std::atomic_size_t> consumer_remaining(node_count, res);
        for (size_t i = 0; i < plan.consumer_counts.size(); ++i) {
            consumer_remaining[i].store(plan.consumer_counts[i], std::memory_order_relaxed);
        }

        auto* parent_trace = profiling::g_current_trace;
        int32_t parent_frame = profiling::g_current_frame;
        auto* parent_counters = profiling::g_current_counters;
        auto* parent_pool = profiling::g_current_framebuffer_pool;

        for (const auto& level : plan.levels) {
            CHRONON_ZONE_C("execute_level", trace_category::kGraph);

            // Sequential pre-resolve phase
            std::pmr::vector<PreResolvedNode> level_resolved(res);
            level_resolved.reserve(level.size());
            for (size_t i = 0; i < level.size(); ++i) {
                level_resolved.emplace_back(res);
                level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
            }

            // Parallel execution phase
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, level.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                        execute_single_node(
                            state, graph, ctx, level_resolved, level[level_index], level_index,
                            parent_trace, parent_frame, parent_counters, parent_pool
                        );
                    }
                }
            );

            // Decrement consumer counts & release dead references
            for (size_t i = 0; i < level.size(); ++i) {
                const GraphNodeId id = level[i];
                for (GraphNodeId input_id : graph.inputs(id)) {
                    if (!contains_index(consumer_remaining, input_id)) {
                        continue;
                    }
                    if (consumer_remaining[input_id].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        state.temp[input_id].reset();
                        state.resolved_key_digest[input_id] = 0;
                        state.resolved_frame_dependent[input_id] = 0;
                        state.resolved_cache_hit[input_id] = 0;
                        state.resolved_bboxes[input_id].reset();
                    }
                }
            }
        }

        return state.temp[output];
    });
}

} // namespace chronon3d::graph
