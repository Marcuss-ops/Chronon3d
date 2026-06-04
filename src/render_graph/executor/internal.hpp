#pragma once

// Internal execution helpers for GraphExecutor.
// NOT part of the public API — do not include outside graph_executor.cpp / graph_executor_internal.cpp.

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include "execution_state.hpp"
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

// contains_index helper is defined in execution_state.hpp


// ── Internal data structures ────────────────────────────────────────


// ── Node execution helpers ──────────────────────────────────────────

PreResolvedNode resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    std::pmr::memory_resource* res
);

CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id,
    bool inputs_all_cache_hits
);

std::optional<raster::BBox> compute_dirty_clip(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const std::optional<raster::BBox>& predicted_bbox
);

double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
    bool use_cache,
    const cache::NodeCacheKey& key,
    CachedFB& result,
    const RenderGraphContext& ctx,
    cache::FramebufferPool* parent_pool
);

void emit_node_records(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const cache::NodeCacheKey& key,
    const CachedFB& result,
    const std::optional<raster::BBox>& clip_rect,
    const std::string& cache_status,
    bool is_cacheable,
    int input_count,
    double duration_ms
);

void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining
);

} // namespace chronon3d::graph
