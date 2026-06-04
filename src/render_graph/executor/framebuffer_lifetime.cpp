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
    if (ctx.tile_execution_enabled && ctx.active_tile_clip) {
        auto owned_fb = ctx.acquire_owned_fb(ctx.width, ctx.height, false);
        owned_fb->clear(Color::transparent());
        Framebuffer* raw = owned_fb.release();
        PoolFbDeleter deleter{nullptr};
        if (ctx.framebuffer_pool) {
            deleter = PoolFbDeleter{ctx.framebuffer_pool.get(), ctx.framebuffer_pool->alive_token()};
        }
        state.shared_transparent = CachedFB(raw, std::move(deleter));
    }
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
    std::pmr::vector<std::atomic_size_t>& consumer_remaining
) {
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

} // namespace chronon3d::graph
