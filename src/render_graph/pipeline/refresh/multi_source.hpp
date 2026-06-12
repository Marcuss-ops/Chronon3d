#pragma once

// ---------------------------------------------------------------------------
// refresh/multi_source.hpp
//
// Refreshes MultiSourceNode payloads when reusing a cached compiled graph.
// Handles layers with multiple source nodes (e.g. multiple shapes in one layer).
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <unordered_map>
#include <string>

namespace chronon3d::graph::detail {

/// Refresh a MultiSourceNode with current frame data.
/// Builds the MultiSourceItem list from the layer's source nodes.
void refresh_multi_source_node(
    MultiSourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
