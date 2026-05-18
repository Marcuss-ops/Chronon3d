#include "layer_builder_internal.hpp"

#include <chronon3d/math/transform.hpp>
#include <utility>

namespace chronon3d::layer_builder_internal {

RenderNode* last_node(Layer& layer) {
    if (layer.nodes.empty()) {
        return nullptr;
    }
    return &layer.nodes.back();
}

void set_last_shadow(Layer& layer, DropShadow shadow) {
    if (auto* node = last_node(layer)) {
        node->shadow = shadow;
    }
}

void set_last_glow(Layer& layer, Glow glow) {
    if (auto* node = last_node(layer)) {
        node->glow = glow;
    }
}

void set_last_position(Layer& layer, Vec3 pos) {
    if (auto* node = last_node(layer)) {
        node->world_transform.position = pos;
    }
}

void set_last_rotation(Layer& layer, Vec3 euler_deg) {
    if (auto* node = last_node(layer)) {
        node->world_transform.rotation = math::from_euler(euler_deg);
    }
}

void set_last_scale(Layer& layer, Vec3 s) {
    if (auto* node = last_node(layer)) {
        node->world_transform.scale = s;
    }
}

void set_last_anchor(Layer& layer, Vec3 a) {
    if (auto* node = last_node(layer)) {
        node->world_transform.anchor = a;
    }
}

void set_last_opacity(Layer& layer, f32 opacity) {
    if (auto* node = last_node(layer)) {
        node->world_transform.opacity = opacity;
    }
}

} // namespace chronon3d::layer_builder_internal
