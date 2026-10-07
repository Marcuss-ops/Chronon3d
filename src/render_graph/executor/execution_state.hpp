#pragma once

#include "text_bbox_reporter.hpp"
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/render_graph/executor/execution_workspace.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
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
    struct LocalStorage {
        std::vector<CachedFB> temp;
        std::vector<u64> resolved_key_digest;
        std::vector<char> resolved_frame_dependent;
        std::vector<char> resolved_cache_hit;
        std::vector<std::optional<raster::BBox>> resolved_bboxes;
        std::vector<OwnedFB> physical_slots;
        std::vector<char> static_bake_member_skip;
        std::vector<std::uint32_t> static_bake_root_index;
    };
    std::unique_ptr<LocalStorage> owned_storage;

    std::vector<CachedFB>& temp;
    std::vector<u64>& resolved_key_digest;
    std::vector<char>& resolved_frame_dependent;
    std::vector<char>& resolved_cache_hit;
    std::vector<std::optional<raster::BBox>>& resolved_bboxes;
    std::vector<OwnedFB>& physical_slots;
    std::vector<char>& static_bake_member_skip;
    std::vector<std::uint32_t>& static_bake_root_index;

    CachedFB shared_transparent;

    // Per-session reporter for text-bbox expansion warnings.  Lives for the
    // duration of a single graph execution; no static/process-wide state.
    TextBboxReporter text_bbox_reporter;
    NodeMemoryTracker* node_memory_tracker{nullptr};

    explicit ExecutionState(ExecutionWorkspace& workspace,
                            NodeMemoryTracker* tracker = nullptr)
        : owned_storage(nullptr),
          temp(workspace.temp), resolved_key_digest(workspace.resolved_key_digest),
          resolved_frame_dependent(workspace.resolved_frame_dependent),
          resolved_cache_hit(workspace.resolved_cache_hit),
          resolved_bboxes(workspace.resolved_bboxes),
          physical_slots(workspace.physical_slots),
          static_bake_member_skip(workspace.static_bake_member_skip),
          static_bake_root_index(workspace.static_bake_root_index),
          node_memory_tracker(tracker) {}

    // Standalone execution state whose vectors live in internally-owned
    // (heap) storage.  Used by tests and isolated executions that do not
    // reuse an ExecutionWorkspace.  The historical
    // `std::pmr::memory_resource*` parameter was removed instead of being
    // silently ignored: LocalStorage holds plain std::vector members, so no
    // allocation is arena-scoped and accepting an arena here would mislead
    // callers.
    explicit ExecutionState(NodeMemoryTracker* tracker = nullptr)
        : owned_storage(std::make_unique<LocalStorage>()),
          temp(owned_storage->temp),
          resolved_key_digest(owned_storage->resolved_key_digest),
          resolved_frame_dependent(owned_storage->resolved_frame_dependent),
          resolved_cache_hit(owned_storage->resolved_cache_hit),
          resolved_bboxes(owned_storage->resolved_bboxes),
          physical_slots(owned_storage->physical_slots),
          static_bake_member_skip(owned_storage->static_bake_member_skip),
          static_bake_root_index(owned_storage->static_bake_root_index),
          node_memory_tracker(tracker) {}
};

struct PreResolvedNode {
    std::pmr::vector<FramebufferRef> inputs;
    std::pmr::vector<std::optional<raster::BBox>> input_bboxes;
    bool inputs_frame_dependent = false;
    bool has_cacheable_inputs = false;
    u64 input_hash = 0;
    explicit PreResolvedNode(std::pmr::memory_resource* res)
        : inputs(res), input_bboxes(res) {}
};

// Cache evaluation outcome.  The status is an enum on the hot path so the
// per-node/frame evaluation never allocates a heap std::string just to feed a
// "hit"/"miss" comparison and telemetry/logging; the legacy token set is
// preserved verbatim by cache_status_name() so telemetry text and
// diagnostics output are unchanged.
enum class CacheEvaluationStatus : std::uint8_t {
    Hit,
    Miss,
    BypassNoCache,
    BypassCompositeForZeroCopy,
    BypassNotCacheable,
    BypassFrameDependent,
};

[[nodiscard]] constexpr std::string_view cache_status_name(
    CacheEvaluationStatus status) noexcept {
    switch (status) {
    case CacheEvaluationStatus::Hit: return "hit";
    case CacheEvaluationStatus::Miss: return "miss";
    case CacheEvaluationStatus::BypassNoCache: return "bypass_no_cache";
    case CacheEvaluationStatus::BypassCompositeForZeroCopy:
        return "bypass_composite_for_zerocopy";
    case CacheEvaluationStatus::BypassNotCacheable: return "bypass_not_cacheable";
    case CacheEvaluationStatus::BypassFrameDependent:
        return "bypass_frame_dependent";
    }
    return "unknown";
}

struct CacheEvalResult {
    CachedFB result;
    NodeCacheKey key;
    CacheEvaluationStatus cache_status{CacheEvaluationStatus::Miss};
    bool node_frame_dependent = false;
    bool use_cache = false;
    bool is_cacheable = false;
};

} // namespace chronon3d::graph
