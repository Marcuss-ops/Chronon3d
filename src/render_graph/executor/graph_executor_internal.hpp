#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/counters.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <vector>
#include <memory>
#include <optional>
#include <atomic>

namespace chronon3d::graph {

template <typename T>
[[nodiscard]] inline bool contains_index(const std::vector<T>& values, GraphNodeId id) {
    return id < values.size();
}

// ── Shared execution state ──────────────────────────────────────
struct ExecutionState {
    std::vector<std::shared_ptr<Framebuffer>> temp;
    std::vector<u64> resolved_key_digest;
    std::vector<char> resolved_frame_dependent;
    std::vector<char> resolved_cache_hit;
    std::vector<std::optional<raster::BBox>> resolved_bboxes;
};

// ── Pre-resolved inputs for a single node ───────────────────────
struct PreResolvedNode {
    std::vector<std::shared_ptr<Framebuffer>> inputs;
    std::vector<std::optional<raster::BBox>> input_bboxes;
    bool inputs_frame_dependent = false;
    bool has_cacheable_inputs = false;
    bool inputs_all_cache_hits = false;
    u64 input_hash = 0;
};

// ── Cache evaluation result ─────────────────────────────────────
struct CacheEvalResult {
    std::shared_ptr<Framebuffer> result;
    cache::NodeCacheKey key;
    std::string cache_status;
    bool node_frame_dependent = false;
    bool use_cache = false;
    bool is_cacheable = false;
};

[[nodiscard]] PreResolvedNode resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::vector<std::atomic_size_t>& consumer_remaining
);

[[nodiscard]] CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id,
    bool inputs_all_cache_hits
);

[[nodiscard]] std::optional<raster::BBox> compute_dirty_clip(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const std::optional<raster::BBox>& predicted_bbox
);

[[nodiscard]] double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    const std::vector<std::shared_ptr<Framebuffer>>& inputs,
    const std::vector<std::optional<raster::BBox>>& input_bboxes,
    bool use_cache,
    const cache::NodeCacheKey& key,
    std::shared_ptr<Framebuffer>& result,
    const RenderGraphContext& ctx
);

void emit_node_records(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const cache::NodeCacheKey& key,
    const std::shared_ptr<Framebuffer>& result,
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
    const std::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index
);

} // namespace chronon3d::graph
