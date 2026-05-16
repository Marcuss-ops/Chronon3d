#include "graph_builder_composite_pass.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_composite_pass(RenderGraph& graph, GraphNodeId& current,
                           GraphNodeId layer_output, const Layer& layer) {
    if (layer_output == k_invalid_node || layer_output == current) return;
    auto composite = graph.add_node(std::make_unique<CompositeNode>(layer.blend_mode));
    graph.connect(current, composite);
    graph.connect(layer_output, composite);
    current = composite;
}

} // namespace chronon3d::graph::detail
