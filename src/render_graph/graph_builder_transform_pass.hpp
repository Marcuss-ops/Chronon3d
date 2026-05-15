#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

/// Append a TransformNode if the layer needs one (projected, precomp, video, or has transform).
void append_transform_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                     const LayerGraphItem& item);

} // namespace chronon3d::graph::detail
