#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <vector>
#include <memory>
#include <optional>
#include <atomic>

namespace chronon3d::graph {

template <typename Container>
[[nodiscard]] inline bool contains_index(const Container& values, GraphNodeId id) {
    return static_cast<size_t>(id) < values.size();
}

// ── Shared execution state ──────────────────────────────────────
struct ExecutionState {
    std::pmr::vector<std::shared_ptr<Framebuffer>> temp;
    std::pmr::vector<u64> resolved_key_digest;
    std::pmr::vector<char> resolved_frame_dependent;
    std::pmr::vector<char> resolved_cache_hit;
    std::pmr::vector<std::optional<raster::BBox>> resolved_bboxes;

    explicit ExecutionState(std::pmr::memory_resource* res)
        : temp(res), resolved_key_digest(res), resolved_frame_dependent(res),
          resolved_cache_hit(res), resolved_bboxes(res) {}
};

// ── Pre-resolved inputs for a single node ───────────────────────
struct PreResolvedNode {
    std::pmr::vector<std::shared_ptr<Framebuffer>> inputs;
    std::pmr::vector<std::optional<raster::BBox>> input_bboxes;
    bool inputs_frame_dependent = false;
    bool has_cacheable_inputs = false;
    bool inputs_all_cache_hits = false;
    u64 input_hash = 0;

    explicit PreResolvedNode(std::pmr::memory_resource* res)
        : inputs(res), input_bboxes(res) {}
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
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    std::pmr::memory_resource* res
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
    std::span<const std::shared_ptr<Framebuffer>> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
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
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index
);

} // namespace chronon3d::graph
