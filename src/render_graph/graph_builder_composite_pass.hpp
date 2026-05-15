#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Blend the layer output into the current frame buffer using the layer's blend mode.
void append_composite_pass(RenderGraph& graph, GraphNodeId& current,
                           GraphNodeId layer_output, const Layer& layer);

} // namespace chronon3d::graph::detail
