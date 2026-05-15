#include "graph_builder_mask_pass.hpp"

#include <chronon3d/render_graph/nodes/mask_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_mask_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                const Layer& layer) {
    if (!layer.mask.enabled()) return;

    auto masked = graph.add_node(std::make_unique<MaskNode>(layer.mask));
    graph.connect(layer_output, masked);
    layer_output = masked;
}

} // namespace chronon3d::graph::detail
