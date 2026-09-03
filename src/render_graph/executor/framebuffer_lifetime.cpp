#include "framebuffer_lifetime.hpp"
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d::graph {

void init_shared_transparent_fb(
    ExecutionState& state,
    const RenderGraphContext& ctx,
    std::pmr::memory_resource* res
) {
    (void)res;
    if (ctx.policy.tile_execution_enabled && ctx.node_exec.active_tile_clip) {
        auto owned_fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, false);
        owned_fb->clear(Color::transparent());
        Framebuffer* raw = owned_fb.release();
        PoolFbDeleter deleter;
        if (ctx.services.framebuffer_pool) {
            deleter = PoolFbDeleter{ctx.services.framebuffer_pool};
        }
        state.shared_transparent = CachedFB(raw, std::move(deleter));
    }
}

std::pmr::vector<std::atomic_size_t> init_consumer_remaining(
    size_t node_count,
    std::span<const CompiledResourcePlan> resources,
    std::pmr::memory_resource* res
) {
    std::pmr::vector<std::atomic_size_t> remaining(node_count, res);
    for (std::size_t i = 0; i < resources.size() && i < remaining.size(); ++i) {
        remaining[i].store(resources[i].consumer_count, std::memory_order_relaxed);
    }
    return remaining;
}

std::pmr::vector<std::atomic_size_t> init_consumer_remaining(
    size_t node_count,
    std::span<const size_t> consumer_counts,
    std::pmr::memory_resource* res
) {
    std::pmr::vector<std::atomic_size_t> remaining(node_count, res);
    for (size_t i = 0; i < consumer_counts.size(); ++i) {
        remaining[i].store(consumer_counts[i], std::memory_order_relaxed);
    }
    return remaining;
}

void release_consumed_framebuffers(
    ExecutionState& state,
    RenderGraph& graph,
    std::span<const GraphNodeId> level,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    std::span<const GraphNodeId> release_after_level
) {
    // Keep the remaining-consumer counters for node input reuse decisions,
    // but make destruction deterministic from the compiled DAG schedule.
    // When no compiled release schedule is supplied (fallback / unit tests),
    // reset dynamically as counters reach zero.
    for (const GraphNodeId id : level) {
        for (GraphNodeId input_id : graph.inputs(id)) {
            if (!contains_index(consumer_remaining, input_id)) {
                continue;
            }
            if (consumer_remaining[input_id].fetch_sub(1, std::memory_order_acq_rel) == 1 &&
                release_after_level.empty()) {
                if (contains_index(state.temp, input_id)) {
                    state.temp[input_id].reset();
                    state.resolved_key_digest[input_id] = 0;
                    state.resolved_frame_dependent[input_id] = 0;
                    state.resolved_cache_hit[input_id] = 0;
                    state.resolved_bboxes[input_id].reset();
                }
            }
        }
    }
    for (const GraphNodeId id : release_after_level) {
        if (!contains_index(state.temp, id)) continue;
        state.temp[id].reset();
        state.resolved_key_digest[id] = 0;
        state.resolved_frame_dependent[id] = 0;
        state.resolved_cache_hit[id] = 0;
        state.resolved_bboxes[id].reset();
    }
}

} // namespace chronon3d::graph
