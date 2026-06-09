#pragma once

#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>

#include <string>

namespace chronon3d::graph {

std::string format_memory(size_t bytes);

std::string build_enriched_dot(
    const RenderGraph& graph,
    const RenderGraphContext& ctx,
    const std::vector<GraphPreflightNode>& node_reports
);

} // namespace chronon3d::graph
