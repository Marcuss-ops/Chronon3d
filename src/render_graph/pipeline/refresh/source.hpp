#pragma once

// ---------------------------------------------------------------------------
// refresh/source.hpp
//
// Refreshes SourceNode payloads when reusing a cached compiled graph.
// Handles both root-level sources and layer-level sources.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <unordered_map>
#include <string>

namespace chronon3d::graph::detail {

/// Refresh a SourceNode with current frame data.
/// Handles two cases:
///   1. Root-level source (layer_id is empty) — uses scene root nodes lookup
///   2. Layer-level source — uses resolved layers lookup
void refresh_source_node(
    SourceNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    const std::unordered_map<std::string, const RenderNode*>& root_nodes_by_name,
    const std::unordered_map<std::string, bool>& is_static_cache,
    RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
