#pragma once

// Internal execution helpers for GraphExecutor.
// NOT part of the public API — do not include outside graph_executor.cpp / graph_executor_internal.cpp.

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

// ── contains_index helper (used by both executor .cpp files) ────────

template <typename Container>
[[nodiscard]] inline bool contains_index(const Container& values, GraphNodeId id) {
    return static_cast<size_t>(id) < values.size();
}

// ── Internal data structures ────────────────────────────────────────

struct ExecutionState {
    // temp holds CachedFB (shared_ptr) for uniform handling of both
    // cache hits and pool-allocated framebuffers.
    std::pmr::vector<CachedFB> temp;
    std::pmr::vector<u64> resolved_key_digest;
    std::pmr::vector<char> resolved_frame_dependent;
    std::pmr::vector<char> resolved_cache_hit;
    std::pmr::vector<std::optional<raster::BBox>> resolved_bboxes;

    explicit ExecutionState(std::pmr::memory_resource* res)
        : temp(res), resolved_key_digest(res), resolved_frame_dependent(res),
          resolved_cache_hit(res), resolved_bboxes(res) {}
};

struct PreResolvedNode {
    // Non-owning FramebufferRef views — no atomic refcounting overhead.
    std::pmr::vector<FramebufferRef> inputs;
    std::pmr::vector<std::optional<raster::BBox>> input_bboxes;
    bool inputs_frame_dependent = false;
    bool has_cacheable_inputs = false;
    bool inputs_all_cache_hits = false;
    u64 input_hash = 0;

    explicit PreResolvedNode(std::pmr::memory_resource* res)
        : inputs(res), input_bboxes(res) {}
};

struct CacheEvalResult {
    CachedFB result;             // non-null on cache hit
    cache::NodeCacheKey key;
    std::string cache_status;
    bool node_frame_dependent = false;
    bool use_cache = false;
    bool is_cacheable = false;
};

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
    cache::FramebufferPool* parent_pool
);

} // namespace chronon3d::graph
