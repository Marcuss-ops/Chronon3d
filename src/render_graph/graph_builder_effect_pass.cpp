#include "graph_builder_effect_pass.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_effect_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                  const Layer& layer, const LayerGraphItem& item,
                                  const Camera2_5D& cam25d) {
    // Layer effects
    if (!layer.effects.empty()) {
        auto effects = graph.add_node(std::make_unique<EffectStackNode>(layer.effects));
        graph.connect(layer_output, effects);
        layer_output = effects;
    }

    // DOF blur (only for projected 2.5D layers)
    if (item.projected && cam25d.dof.enabled) {
        const f32 world_z = item.depth + cam25d.position.z;
        const f32 dist = std::abs(world_z - cam25d.dof.focus_z);
        const f32 blur = std::min(dist * cam25d.dof.aperture, cam25d.dof.max_blur);

        if (blur > 0.5f) {
            EffectStack dof_stack;
            dof_stack.push_back(EffectInstance{BlurParams{blur}});
            auto dof_node = graph.add_node(std::make_unique<EffectStackNode>(std::move(dof_stack)));
            graph.connect(layer_output, dof_node);
            layer_output = dof_node;
        }
    }
}

} // namespace chronon3d::graph::detail
