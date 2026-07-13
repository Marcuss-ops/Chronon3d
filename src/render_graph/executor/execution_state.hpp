#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {
using ::chronon3d::cache::FramebufferPool;
using ::chronon3d::cache::NodeCacheKey;

// ── contains_index helper ───────────────────────────────────────────

template <typename Container>
[[nodiscard]] inline bool contains_index(const Container& values, GraphNodeId id) {
    return static_cast<size_t>(id) < values.size();
}

// ── Internal data structures ────────────────────────────────────────

struct ExecutionState {
    std::pmr::vector<CachedFB> temp;
    std::pmr::vector<u64> resolved_key_digest;
    std::pmr::vector<char> resolved_frame_dependent;
    std::pmr::vector<char> resolved_cache_hit;
    std::pmr::vector<std::optional<raster::BBox>> resolved_bboxes;
    CachedFB shared_transparent;

    explicit ExecutionState(std::pmr::memory_resource* res)
        : temp(res), resolved_key_digest(res), resolved_frame_dependent(res),
          resolved_cache_hit(res), resolved_bboxes(res) {}
};

struct PreResolvedNode {
    std::pmr::vector<FramebufferRef> inputs;
    std::pmr::vector<std::optional<raster::BBox>> input_bboxes;
    bool inputs_frame_dependent = false;
    bool has_cacheable_inputs = false;
    u64 input_hash = 0;
    // Effective opacity at the current frame, evaluated at resolve-time from
    // the layer's m_layer.anim_transform.opacity. Defaults to 1.0 to keep
    // existing call sites safe until populate_inputs_invisible_layers() is
    // wired inside executor_levels.cpp::resolve_layer pass.
    f32 resolved_opacity = 1.0f;

    explicit PreResolvedNode(std::pmr::memory_resource* res)
        : inputs(res), input_bboxes(res) {}
};

struct CacheEvalResult {
    CachedFB result;
    NodeCacheKey key;
    std::string cache_status;
    bool node_frame_dependent = false;
    bool use_cache = false;
    bool is_cacheable = false;
};

} // namespace chronon3d::graph
