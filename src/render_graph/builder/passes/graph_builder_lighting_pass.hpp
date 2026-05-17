#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>

namespace chronon3d::graph::detail {

void append_lighting_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                    const LayerGraphItem& item,
                                    const RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
