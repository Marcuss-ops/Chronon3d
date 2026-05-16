#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Create the source sub-graph for a single layer (Normal / Precomp / Video).
GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
