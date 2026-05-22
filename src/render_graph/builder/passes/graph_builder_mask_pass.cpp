#include "graph_builder_mask_pass.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_mask_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                const LayerGraphItem& item,
                                const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;
    if (!layer.mask.enabled()) return;

    const bool is_static = layer.cache_static || item.is_static;
    auto masked = graph.add_node(std::make_unique<MaskNode>(layer.mask, is_static ? Frame{0} : Frame{-1}));
    graph.node(masked).set_frame_dependent(!is_static);
    graph.connect(layer_output, masked);
    layer_output = masked;
}

} // namespace chronon3d::graph::detail
