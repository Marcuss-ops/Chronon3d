#pragma once

// ---------------------------------------------------------------------------
// refresh/transform.hpp
//
// Refreshes TransformNode payloads when reusing a cached compiled graph.
// Updates the transform matrix and opacity from the current frame's
// evaluated layer state, handling 2.5D projection and centered rendering.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <unordered_map>
#include <string>

namespace chronon3d::graph::detail {

/// Refresh a TransformNode with current frame data.
/// Handles projected (2.5D camera) and non-projected paths.
void refresh_transform_node(
    TransformNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
