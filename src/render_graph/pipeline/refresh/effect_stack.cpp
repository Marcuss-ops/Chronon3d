#include "effect_stack.hpp"

#include <chronon3d/scene/model/layer/layer.hpp>

namespace chronon3d::graph::detail {

void refresh_effect_stack_node(
    EffectStackNode& node,
    const std::unordered_map<std::string_view, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx)
{
    const std::string layer_id{node.layer_id()};
    if (layer_id.empty()) return;

    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const Layer& layer = *layer_it->second->layer;
    const auto& sampled_effects = layer.effects();
    auto& node_effects = node.effects();
    // The graph is reused across frame evaluations. Effect parameters that
    // are sampled from RenderPlan tracks are already current on the resolved
    // scene layer, so refresh the corresponding node instances before graph
    // execution. Keeping the compiled stack's topology is safe here because
    // changing effect count/type changes the graph topology and rebuilds it.
    if (sampled_effects.size() == node_effects.size()) {
        for (std::size_t index = 0; index < sampled_effects.size(); ++index) {
            if (sampled_effects[index].effect_type == node_effects[index].effect_type)
                node_effects[index] = sampled_effects[index];
        }
    }
    if (layer.anim_transform.blur.is_time_dependent()) {
        const SampleTime local_time = layer.local_time(ctx.frame_input.sample_time);
        const f32 blur_radius = layer.anim_transform.blur.evaluate(local_time);
        for (auto& effect : node.effects()) {
            if (auto* blur = std::get_if<BlurParams>(&effect.params)) {
                blur->radius = blur_radius;
            }
        }
    }
}

} // namespace chronon3d::graph::detail
