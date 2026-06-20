#pragma once

#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>

#include <optional>
#include <string>

namespace chronon3d::graph {

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY

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

#else

/// No-op stub — telemetry persistence is disabled at compile time.
/// The sharded in-memory stores are still active (for runtime counters)
/// but the heavy record construction in telemetry_emitter.cpp is skipped.
inline void emit_node_records(
    const RenderGraphContext& /*ctx*/,
    const RenderGraphNode& /*node*/,
    const cache::NodeCacheKey& /*key*/,
    const CachedFB& /*result*/,
    const std::optional<raster::BBox>& /*clip_rect*/,
    const std::string& /*cache_status*/,
    bool /*is_cacheable*/,
    int /*input_count*/,
    double /*duration_ms*/
) {}

#endif // CHRONON3D_ENABLE_SQLITE_TELEMETRY

} // namespace chronon3d::graph
