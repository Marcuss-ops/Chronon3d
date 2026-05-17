#include "graph_builder_lighting_pass.hpp"

#include <chronon3d/render_graph/nodes/lighting_node.hpp>

#include <string>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_lighting_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                    const LayerGraphItem& item,
                                    const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;

    if (!item.projected) {
        return;
    }
    if (!ctx.light_context.enabled) {
        return;
    }
    if (item.native_3d) {
        return;
    }
    if (!layer.material.accepts_lights) {
        return;
    }

    auto lighting_node = graph.add_node(
        LightingNode::create(std::string(layer.name), item.world_matrix, layer.material));
    graph.connect(layer_output, lighting_node);
    layer_output = lighting_node;
}

} // namespace chronon3d::graph::detail
