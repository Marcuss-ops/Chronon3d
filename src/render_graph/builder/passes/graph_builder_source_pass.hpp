#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Create the source sub-graph for a single layer (Normal / Precomp / Video).
GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx,
                               const BuilderContext& node_ctx = {});

} // namespace chronon3d::graph::detail
