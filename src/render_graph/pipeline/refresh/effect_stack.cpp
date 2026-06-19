#include "effect_stack.hpp"

#include <chronon3d/scene/model/layer/layer.hpp>

namespace chronon3d::graph::detail {

void refresh_effect_stack_node(
    EffectStackNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx)
{
    const std::string layer_id{node.layer_id()};
    if (layer_id.empty()) return;

    const auto layer_it = resolved_by_name.find(layer_id);
    if (layer_it == resolved_by_name.end() || !layer_it->second || !layer_it->second->layer) {
        return;
    }

    const Layer& layer = *layer_it->second->layer;
    if (layer.anim_transform.blur.is_time_dependent()) {
        const Frame local_frame = layer.local_frame(ctx.frame.frame);
        const f32 blur_radius = layer.anim_transform.blur.evaluate(local_frame);
        for (auto& effect : node.effects()) {
            if (auto* blur = std::get_if<BlurParams>(&effect.params)) {
                blur->radius = blur_radius;
            }
        }
    }
}

} // namespace chronon3d::graph::detail
