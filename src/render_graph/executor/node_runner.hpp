#pragma once

#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>

#include <atomic>
#include <optional>
#include <span>
#include <vector>
#include <memory>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

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

void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    double* out_cache_ms = nullptr,
    double* out_dirty_ms = nullptr,
    double* out_telemetry_ms = nullptr,
    double* out_execute_ms = nullptr,
    double* out_predicted_bbox_ms = nullptr,
    double* out_clone_context_ms = nullptr,
    double* out_state_assign_ms = nullptr
);

} // namespace chronon3d::graph
