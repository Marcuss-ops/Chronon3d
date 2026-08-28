#pragma once

#include "execution_state.hpp"
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <optional>

namespace chronon3d::graph {

// Per-node clip calculation only. This helper must not decide whether tile
// pruning is enabled; ExecutionResolver owns that frame-level policy.
std::optional<raster::BBox> compute_dirty_clip(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const std::optional<raster::BBox>& predicted_bbox
);

} // namespace chronon3d::graph
