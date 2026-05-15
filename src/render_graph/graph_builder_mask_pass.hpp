#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Append a MaskNode if the layer has an enabled mask.
void append_mask_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                const Layer& layer);

} // namespace chronon3d::graph::detail
