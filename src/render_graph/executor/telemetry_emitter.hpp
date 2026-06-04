#pragma once

#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>

#include <optional>
#include <string>

namespace chronon3d::graph {

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

} // namespace chronon3d::graph
