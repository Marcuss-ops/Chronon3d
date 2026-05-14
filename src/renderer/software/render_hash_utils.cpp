#include "render_hash_utils.hpp"
#include <chronon3d/renderer/software/render_graph.hpp>
#include <chronon3d/scene/layer_effect.hpp>
#include <chronon3d/scene/effect_stack.hpp>
#include <type_traits>

namespace chronon3d {
namespace renderer {

using render_graph::hash_combine;
using render_graph::hash_bytes;
using render_graph::hash_vec2;
using render_graph::hash_vec3;
using render_graph::hash_color;
using render_graph::hash_string;
using render_graph::hash_transform;

template <typename T>
u64 hash_value_local(const T& value) {
    return hash_bytes(&value, sizeof(T));
}

u64 hash_mask(const Mask& mask) {
    u64 seed = hash_value_local(static_cast<u64>(mask.type));
    seed = hash_combine(seed, hash_vec3(mask.pos));
    seed = hash_combine(seed, hash_vec2(mask.size));
    seed = hash_combine(seed, hash_value_local(mask.radius));
    seed = hash_combine(seed, hash_value_local(mask.inverted));
    return seed;
}

u64 hash_shape(const Shape& shape) {
    u64 seed = hash_value_local(static_cast<u64>(shape.type));
    switch (shape.type) {
        case ShapeType::Rect:
            seed = hash_combine(seed, hash_vec2(shape.rect.size));
            break;
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(shape.rounded_rect.size));
            seed = hash_combine(seed, hash_value_local(shape.rounded_rect.radius));
            break;
        case ShapeType::Circle:
            seed = hash_combine(seed, hash_value_local(shape.circle.radius));
            break;
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(shape.line.to));
            seed = hash_combine(seed, hash_value_local(shape.line.thickness));
            break;
        case ShapeType::Text:
            seed = hash_combine(seed, hash_string(shape.text.text));
            seed = hash_combine(seed, hash_string(shape.text.style.font_path));
            seed = hash_combine(seed, hash_value_local(shape.text.style.size));
            seed = hash_combine(seed, hash_color(shape.text.style.color));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.text.style.align)));
            seed = hash_combine(seed, hash_value_local(shape.text.style.line_height));
            seed = hash_combine(seed, hash_value_local(shape.text.style.tracking));
            seed = hash_combine(seed, hash_value_local(shape.text.style.max_lines));
            seed = hash_combine(seed, hash_value_local(shape.text.style.auto_scale));
            seed = hash_combine(seed, hash_value_local(shape.text.style.min_size));
            seed = hash_combine(seed, hash_value_local(shape.text.style.max_size));
            seed = hash_combine(seed, hash_vec2(shape.text.box.size));
            seed = hash_combine(seed, hash_value_local(shape.text.box.enabled));
            break;
        case ShapeType::Image:
            seed = hash_combine(seed, hash_string(shape.image.path));
            seed = hash_combine(seed, hash_vec2(shape.image.size));
            seed = hash_combine(seed, hash_value_local(shape.image.opacity));
            break;
        case ShapeType::FakeBox3D:
            seed = hash_combine(seed, hash_vec3(shape.fake_box3d.world_pos));
            seed = hash_combine(seed, hash_vec2(shape.fake_box3d.size));
            seed = hash_combine(seed, hash_value_local(shape.fake_box3d.depth));
            seed = hash_combine(seed, hash_color(shape.fake_box3d.color));
            seed = hash_combine(seed, hash_value_local(shape.fake_box3d.top_tint));
            seed = hash_combine(seed, hash_value_local(shape.fake_box3d.side_tint));
            break;
        case ShapeType::GridPlane:
            seed = hash_combine(seed, hash_vec3(shape.grid_plane.world_pos));
            seed = hash_combine(seed, hash_value_local(shape.grid_plane.extent));
            seed = hash_combine(seed, hash_value_local(shape.grid_plane.spacing));
            seed = hash_combine(seed, hash_color(shape.grid_plane.color));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.grid_plane.axis)));
            break;
        default: break;
    }
    return seed;
}

u64 hash_drop_shadow(const DropShadow& shadow) {
    u64 seed = hash_value_local(shadow.enabled);
    seed = hash_combine(seed, hash_vec2(shadow.offset));
    seed = hash_combine(seed, hash_color(shadow.color));
    seed = hash_combine(seed, hash_value_local(shadow.radius));
    return seed;
}

u64 hash_glow(const Glow& glow) {
    u64 seed = hash_value_local(glow.enabled);
    seed = hash_combine(seed, hash_value_local(glow.radius));
    seed = hash_combine(seed, hash_value_local(glow.intensity));
    seed = hash_combine(seed, hash_color(glow.color));
    return seed;
}

u64 hash_node(const RenderNode& node) {
    u64 seed = hash_string(node.name);
    seed = hash_combine(seed, hash_transform(node.world_transform));
    seed = hash_combine(seed, hash_color(node.color));
    seed = hash_combine(seed, hash_shape(node.shape));
    seed = hash_combine(seed, hash_drop_shadow(node.shadow));
    seed = hash_combine(seed, hash_glow(node.glow));
    seed = hash_combine(seed, hash_value_local(node.visible));
    return seed;
}

u64 hash_effect_stack(const EffectStack& stack) {
    u64 seed = hash_value_local(stack.size());
    for (const auto& inst : stack) {
        seed = hash_combine(seed, hash_value_local(inst.enabled));
        std::visit([&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BlurParams>) {
                seed = hash_combine(seed, hash_value_local(p.radius));
            } else if constexpr (std::is_same_v<T, TintParams>) {
                seed = hash_combine(seed, hash_color(p.color));
                seed = hash_combine(seed, hash_value_local(p.amount));
            } else if constexpr (std::is_same_v<T, BrightnessParams>) {
                seed = hash_combine(seed, hash_value_local(p.value));
            } else if constexpr (std::is_same_v<T, ContrastParams>) {
                seed = hash_combine(seed, hash_value_local(p.value));
            } else if constexpr (std::is_same_v<T, DropShadowParams>) {
                seed = hash_combine(seed, hash_vec2(p.offset));
                seed = hash_combine(seed, hash_color(p.color));
                seed = hash_combine(seed, hash_value_local(p.radius));
            } else if constexpr (std::is_same_v<T, GlowParams>) {
                seed = hash_combine(seed, hash_value_local(p.radius));
                seed = hash_combine(seed, hash_value_local(p.intensity));
                seed = hash_combine(seed, hash_color(p.color));
            } else if constexpr (std::is_same_v<T, BloomParams>) {
                seed = hash_combine(seed, hash_value_local(p.threshold));
                seed = hash_combine(seed, hash_value_local(p.radius));
                seed = hash_combine(seed, hash_value_local(p.intensity));
            }
        }, inst.params);
    }
    return seed;
}

u64 hash_camera_2_5d(const Camera2_5D& camera) {
    u64 seed = hash_value_local(camera.enabled);
    seed = hash_combine(seed, hash_vec3(camera.position));
    seed = hash_combine(seed, hash_vec3(camera.point_of_interest));
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(camera.projection_mode)));
    seed = hash_combine(seed, hash_value_local(camera.zoom));
    seed = hash_combine(seed, hash_value_local(camera.fov_deg));
    seed = hash_combine(seed, hash_value_local(camera.dof.enabled));
    seed = hash_combine(seed, hash_value_local(camera.dof.focus_z));
    seed = hash_combine(seed, hash_value_local(camera.dof.aperture));
    seed = hash_combine(seed, hash_value_local(camera.dof.max_blur));
    return seed;
}

u64 hash_camera(const Camera& camera) {
    u64 seed = hash_transform(camera.transform);
    seed = hash_combine(seed, hash_value_local(camera.fov_deg));
    seed = hash_combine(seed, hash_value_local(camera.near_plane));
    seed = hash_combine(seed, hash_value_local(camera.far_plane));
    return seed;
}

u64 hash_layer(const Layer& layer) {
    u64 seed = hash_string(layer.name);
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(layer.kind)));
    seed = hash_combine(seed, hash_transform(layer.transform));
    seed = hash_combine(seed, hash_value_local(layer.from));
    seed = hash_combine(seed, hash_value_local(layer.duration));
    seed = hash_combine(seed, hash_value_local(layer.visible));
    seed = hash_combine(seed, hash_value_local(layer.is_3d));
    seed = hash_combine(seed, hash_mask(layer.mask));
    seed = hash_combine(seed, hash_effect_stack(layer.effects));
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(layer.blend_mode)));
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(layer.depth_role)));
    seed = hash_combine(seed, hash_value_local(layer.depth_offset));
    seed = hash_combine(seed, hash_value_local(layer.nodes.size()));
    for (const auto& node : layer.nodes) {
        seed = hash_combine(seed, hash_node(node));
    }
    return seed;
}

} // namespace renderer
} // namespace chronon3d
