#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Append effect stack nodes for layer effects and optionally DOF blur.
void append_effect_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                  const Layer& layer, const LayerGraphItem& item,
                                  const Camera2_5DRuntime& cam25d);

} // namespace chronon3d::graph::detail
