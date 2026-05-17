#include "graph_builder_effect_pass.hpp"
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/render_graph/nodes/dof_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_effect_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                  const Layer& layer, const LayerGraphItem& item,
                                  const Camera2_5DRuntime& cam25d) {
    // Layer effects - now modularly created via registry
    for (const auto& effect : layer.effects) {
        if (!effect.enabled) continue;
        
        auto node_id = graph.add_node(effects::EffectRegistry::instance().create_node(effect));
        graph.connect(layer_output, node_id);
        layer_output = node_id;
    }

    // DOF blur (only for projected 2.5D layers) - logic moved to DofEffectNode
    if (item.projected && cam25d.dof.enabled) {
        auto dof_node = graph.add_node(DofEffectNode::create(cam25d, item.world_z));
        graph.connect(layer_output, dof_node);
        layer_output = dof_node;
    }
}

} // namespace chronon3d::graph::detail
