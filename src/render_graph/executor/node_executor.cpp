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
#include <chronon3d/render_graph/core/node_identity.hpp>       // StableNodeId (trace annotation)
#include <chronon3d/core/memory/framebuffer_handle.hpp>        // Full def of PoolFbDeleter + OwnedFB
#include <chronon3d/cache/framebuffer_pool.hpp>                // Full def required for parent_pool->shared_from_this() (P1 step 2 hardening — explicit dependency, not transitive-resolved via execution_state.hpp)
#include <chronon3d/core/profiling/profiling.hpp>              // profiling::now(), profiling::duration_ms(), CHRONON_TRACE_SCOPE
#include <chronon3d/core/profiling/counters.hpp>              // ctx.node_exec.counters->nodes_executed (atomic fetch_add)
#include <spdlog/spdlog.h>

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
    FramebufferPool* parent_pool,
    const StableNodeId* stable_node_id
) {
    (void)parent_pool; // reclaim policy is carried by OwnedFB's deleter
    if (result) {
        return 0.001;
    }

    // HOT-PATH TAX B — the returned duration feeds telemetry emission only;
    // with no counters sink attached there is no consumer for it, so the two
    // profiling::now() clock reads are skipped entirely on the production hot
    // path (mirrors the cache_evaluator scope).
    const bool timing_enabled = ctx.node_exec.counters != nullptr;
    const auto exec_t0 = timing_enabled
        ? profiling::now()
        : profiling::Clock::time_point{};
    OwnedFB owned;
    {
        // chronon.node is a debug/slow category: node execution only shows
        // in kNodes/kFull traces, keeping light pipeline traces lean (plan §6).
        // stable_node_id annotation (plan §7): 0 when the node has no stable
        // id (precomp/isolated paths) — report tooling filters 0 out.
        const std::uint64_t stable_id =
            stable_node_id ? stable_node_id->value : 0;
        CHRONON_TRACE_SCOPE_ANNOTATED("chronon.node", "node_execute",
                                      "stable_node_id", stable_id);
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
            const auto exec_t1 = timing_enabled
                ? profiling::now()
                : profiling::Clock::time_point{};
            return timing_enabled ? profiling::duration_ms(exec_t0, exec_t1) : 0.0;
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
    const auto exec_t1 = timing_enabled
        ? profiling::now()
        : profiling::Clock::time_point{};
    return timing_enabled ? profiling::duration_ms(exec_t0, exec_t1) : 0.0;
}

} // namespace chronon3d::graph
