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
#include <chronon3d/cache/framebuffer_pool.hpp>                // Full def for FramebufferPool: pool-backed deleter bookkeeping below needs the complete type available to this TU
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
    const StableNodeId* stable_node_id,
    std::uint64_t* local_nodes_executed
) {
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
    const auto record_node_executed = [&]() {
        if (local_nodes_executed) {
            ++(*local_nodes_executed);
        } else if (ctx.node_exec.counters) {
            ctx.node_exec.counters->nodes_executed.fetch_add(
                1, std::memory_order_relaxed);
        }
    };
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
            record_node_executed();
            const auto exec_t1 = timing_enabled
                ? profiling::now()
                : profiling::Clock::time_point{};
            return timing_enabled ? profiling::duration_ms(exec_t0, exec_t1) : 0.0;
        }
        owned = exec_result.take_value();
    }
    record_node_executed();
    if (owned) {
        owned->set_key_digest(key.digest());

        // The output's storage contract must outlive this graph execution
        // before it may enter NodeCache. Arena-backed framebuffers are
        // non-owning views into the rotating video-export arena; retaining the
        // wrapper across frames leaves a dangling/reused pixel pointer once the
        // writer releases and the arena is reset. The predicate also excludes
        // scratch and renderer-owned storage and native-only surfaces.
        const bool persistently_cacheable = node_output_is_persistently_cacheable(
            *owned, owned.get_deleter());
        result = promote_to_cached(std::move(owned));

        if (use_cache && ctx.execution_node_cache() && persistently_cacheable) {
            ctx.execution_node_cache()->store(key, result);
        }
    }
    const auto exec_t1 = timing_enabled
        ? profiling::now()
        : profiling::Clock::time_point{};
    return timing_enabled ? profiling::duration_ms(exec_t0, exec_t1) : 0.0;
}

} // namespace chronon3d::graph
