#include "render_hash_utils.hpp"
#include <chronon3d/scene/effects/layer_effect.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <type_traits>

namespace chronon3d {
namespace renderer {

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_bytes;
using chronon3d::graph::hash_vec2;
using chronon3d::graph::hash_vec3;
using chronon3d::graph::hash_color;
using chronon3d::graph::hash_string;
using chronon3d::graph::hash_transform;

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
        case ShapeType::Path:
            seed = hash_combine(seed, hash_value_local(shape.path.commands.size()));
            for (const auto& cmd : shape.path.commands) {
                seed = hash_combine(seed, hash_value_local(static_cast<u64>(cmd.type)));
                seed = hash_combine(seed, hash_vec2(cmd.p0));
                seed = hash_combine(seed, hash_vec2(cmd.p1));
                seed = hash_combine(seed, hash_vec2(cmd.p2));
            }
            seed = hash_combine(seed, hash_value_local(shape.path.stroke.width));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.path.stroke.cap)));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.path.stroke.join)));
            seed = hash_combine(seed, hash_value_local(shape.path.stroke.trim_start));
            seed = hash_combine(seed, hash_value_local(shape.path.stroke.trim_end));
            seed = hash_combine(seed, hash_value_local(shape.path.closed));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.path.fill.type)));
            seed = hash_combine(seed, hash_color(shape.path.fill.solid));
            seed = hash_combine(seed, hash_vec2(shape.path.fill.gradient.from));
            seed = hash_combine(seed, hash_vec2(shape.path.fill.gradient.to));
            seed = hash_combine(seed, hash_value_local(shape.path.fill.gradient.stops.size()));
            for (const auto& stop : shape.path.fill.gradient.stops) {
                seed = hash_combine(seed, hash_value_local(stop.offset));
                seed = hash_combine(seed, hash_color(stop.color));
            }
            break;

        case ShapeType::Image:
            seed = hash_combine(seed, hash_string(shape.image.path));
            seed = hash_combine(seed, hash_vec2(shape.image.size));
            seed = hash_combine(seed, hash_value_local(shape.image.opacity));
            break;
        case ShapeType::GridBackground:
            seed = hash_combine(seed, hash_vec2(shape.grid_background.size));
            seed = hash_combine(seed, hash_vec2(shape.grid_background.offset));
            seed = hash_combine(seed, hash_color(shape.grid_background.bg_color));
            seed = hash_combine(seed, hash_color(shape.grid_background.grid_color));
            seed = hash_combine(seed, hash_value_local(shape.grid_background.spacing));
            seed = hash_combine(seed, hash_value_local(shape.grid_background.minor_thickness));
            seed = hash_combine(seed, hash_value_local(shape.grid_background.major_thickness));
            seed = hash_combine(seed, hash_value_local(shape.grid_background.major_every));
            seed = hash_combine(seed, hash_value_local(shape.grid_background.centered));
            break;
        case ShapeType::Text:
            seed = hash_combine(seed, hash_string(shape.text.text));
            seed = hash_combine(seed, hash_string(shape.text.style.font_path));
            seed = hash_combine(seed, hash_string(shape.text.style.font_family));
            seed = hash_combine(seed, hash_value_local(shape.text.style.font_weight));
            seed = hash_combine(seed, hash_string(shape.text.style.font_style));
            seed = hash_combine(seed, hash_vec2(shape.text.box.size));
            seed = hash_combine(seed, hash_value_local(shape.text.box.enabled));
            seed = hash_combine(seed, hash_value_local(shape.text.style.size));
            seed = hash_combine(seed, hash_color(shape.text.style.color));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.text.style.align)));
            seed = hash_combine(seed, hash_value_local(shape.text.style.line_height));
            seed = hash_combine(seed, hash_value_local(shape.text.style.tracking));
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
        seed = hash_combine(seed, hash_string(inst.descriptor.id));
        
        if (auto* p = std::any_cast<BlurParams>(&inst.params)) {
            seed = hash_combine(seed, hash_value_local(p->radius));
        } else if (auto* p = std::any_cast<TintParams>(&inst.params)) {
            seed = hash_combine(seed, hash_color(p->color));
            seed = hash_combine(seed, hash_value_local(p->amount));
        } else if (auto* p = std::any_cast<BrightnessParams>(&inst.params)) {
            seed = hash_combine(seed, hash_value_local(p->value));
        } else if (auto* p = std::any_cast<ContrastParams>(&inst.params)) {
            seed = hash_combine(seed, hash_value_local(p->value));
        } else if (auto* p = std::any_cast<DropShadowParams>(&inst.params)) {
            seed = hash_combine(seed, hash_vec2(p->offset));
            seed = hash_combine(seed, hash_color(p->color));
            seed = hash_combine(seed, hash_value_local(p->radius));
        } else if (auto* p = std::any_cast<GlowParams>(&inst.params)) {
            seed = hash_combine(seed, hash_value_local(p->radius));
            seed = hash_combine(seed, hash_value_local(p->intensity));
            seed = hash_combine(seed, hash_color(p->color));
            seed = hash_combine(seed, hash_value_local(p->threshold));
            seed = hash_combine(seed, hash_value_local(p->spread));
            seed = hash_combine(seed, hash_value_local(p->softness));
            seed = hash_combine(seed, hash_value_local(p->falloff));
            seed = hash_combine(seed, hash_value_local(p->core_strength));
            seed = hash_combine(seed, hash_value_local(p->aura_strength));
            seed = hash_combine(seed, hash_value_local(p->bloom_strength));
            seed = hash_combine(seed, hash_value_local(p->additive));
        } else if (auto* p = std::any_cast<BloomParams>(&inst.params)) {
            seed = hash_combine(seed, hash_value_local(p->threshold));
            seed = hash_combine(seed, hash_value_local(p->radius));
            seed = hash_combine(seed, hash_value_local(p->intensity));
        } else if (auto* p = std::any_cast<Fake3DWaveParams>(&inst.params)) {
            seed = hash_combine(seed, hash_value_local(p->amplitude_px));
            seed = hash_combine(seed, hash_value_local(p->frequency));
            seed = hash_combine(seed, hash_value_local(p->speed));
            seed = hash_combine(seed, hash_value_local(p->depth_px));
            seed = hash_combine(seed, hash_value_local(p->phase));
            seed = hash_combine(seed, hash_value_local(p->slices));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(p->axis)));
            seed = hash_combine(seed, hash_value_local(p->perspective));
            seed = hash_combine(seed, hash_value_local(p->highlight));
            seed = hash_combine(seed, hash_value_local(p->side_darkening));
            seed = hash_combine(seed, hash_value_local(p->shadow_enabled));
            seed = hash_combine(seed, hash_color(p->shadow_color));
            seed = hash_combine(seed, hash_vec2(p->shadow_offset));
            seed = hash_combine(seed, hash_value_local(p->shadow_blur));
            seed = hash_combine(seed, hash_value_local(p->expand_bounds));
        }
    }
    return seed;
}

u64 hash_camera_2_5d(const Camera2_5D& camera) {
    u64 seed = hash_value_local(camera.enabled);
    seed = hash_combine(seed, hash_vec3(camera.position));
    seed = hash_combine(seed, hash_vec3(camera.point_of_interest));
    seed = hash_combine(seed, hash_value_local(camera.point_of_interest_enabled));
    seed = hash_combine(seed, hash_vec3(camera.rotation));
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
