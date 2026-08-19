// ============================================================================
// node_executor.cpp — run_node implementation.
//
// Source-accurate transcription of the body that previously lived inline
// in src/render_graph/executor/node_runner.cpp:21-104.  Behavior preservation
// is byte-level: no field reordering, no early-return rewritten, no helper
// inlined, no logging added.
// ============================================================================

#include "node_executor.hpp"

#include <chronon3d/render_graph/render_graph_context.hpp>     // RenderGraphContext fields (services, frame_error, node_exec)
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>  // RenderGraphNode::execute() + kind()
#include <chronon3d/core/memory/framebuffer_handle.hpp>        // Full def of PoolFbDeleter + OwnedFB
#include <chronon3d/cache/framebuffer_pool.hpp>                // Full def required for parent_pool->shared_from_this() (P1 step 2 hardening — explicit dependency, not transitive-resolved via execution_state.hpp)
#include <chronon3d/core/profiling/profiling.hpp>              // profiling::now(), profiling::duration_ms(), CHRONON_ZONE_C
#include <chronon3d/core/profiling/counters.hpp>              // ctx.node_exec.counters->nodes_executed (atomic fetch_add)

namespace chronon3d::graph {

double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
    bool use_cache,
    const ::chronon3d::cache::NodeCacheKey& key,
    CachedFB& result,
    const RenderGraphContext& ctx,
    FramebufferPool* parent_pool
) {
    (void)parent_pool; // reclaim policy is carried by OwnedFB's deleter
    if (result) {
        return 0.001;
    }

    const auto exec_t0 = profiling::now();
    OwnedFB owned;
    {
        CHRONON_ZONE_C("node_execute", trace_category::kGraph);
        auto exec_result = node.execute(node_ctx, inputs, input_bboxes);
        if (!exec_result) {
            // P0-1 — node execution failed; write error to the shared
            // frame_error slot so the executor can propagate it to
            // frame-level failure (GraphExecutor returns nullptr).
            if (ctx.frame_error) {
                *ctx.frame_error = exec_result.error();
            }
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
            }
            const auto exec_t1 = profiling::now();
            return profiling::duration_ms(exec_t0, exec_t1);
        }
        owned = exec_result.take_value();
    }
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
    if (owned) {
        owned->set_key_digest(key.digest());

        // ── Transform scratch buffer: preserve the scratch_slot deleter ──
        //    through the CachedFB so the buffer is restored to the scratch
        //    slot (cleared) when all consumers finish, instead of being
        //    released to the pool.  Also skip caching — caching the scratch
        //    would allow stale content to survive past the frame boundary.
        const bool is_scratch = std::holds_alternative<ReturnToScratch>(owned.get_deleter().policy);
        // Planned physical-slot results are non-owning RendererOwned views.
        // Their backing slot dies with this ExecutionState, so retaining the
        // view in NodeCache would leave a dangling framebuffer/native handle
        // that fails on a later warm job. Only cache independently-owned FBs.
        const bool is_renderer_owned = std::holds_alternative<RendererOwned>(
            owned.get_deleter().policy);
        result = promote_to_cached(std::move(owned));
        const bool has_native_surface = result &&
            result->surface_handle() != runtime::kInvalidRenderSurfaceHandle;

        if (use_cache && ctx.services.node_cache && !is_scratch &&
            !is_renderer_owned && !has_native_surface) {
            ctx.services.node_cache->store(key, result);
        }
    }
    const auto exec_t1 = profiling::now();
    return profiling::duration_ms(exec_t0, exec_t1);
}

} // namespace chronon3d::graph
