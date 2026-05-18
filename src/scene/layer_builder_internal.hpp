#pragma once

#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::layer_builder_internal {

[[nodiscard]] RenderNode* last_node(Layer& layer);

void set_last_shadow(Layer& layer, DropShadow shadow);
void set_last_glow(Layer& layer, Glow glow);
void set_last_position(Layer& layer, Vec3 pos);
void set_last_rotation(Layer& layer, Vec3 euler_deg);
void set_last_scale(Layer& layer, Vec3 s);
void set_last_anchor(Layer& layer, Vec3 a);
void set_last_opacity(Layer& layer, f32 opacity);

} // namespace chronon3d::layer_builder_internal
