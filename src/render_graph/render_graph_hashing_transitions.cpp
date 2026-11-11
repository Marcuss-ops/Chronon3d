#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

[[nodiscard]] u64 hash_layer_transition_parameters(const LayerTransitionParameters& params) {
    return std::visit([](const auto& p) -> u64 {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return 0ULL;
        } else if constexpr (std::is_same_v<T, CrossfadeParams>) {
            return 0xcbf29ce484222325ULL;
        } else if constexpr (std::is_same_v<T, WipeLinearParams>) {
            return 0xcbf29ce484222326ULL;
        } else if constexpr (std::is_same_v<T, SlideParams>) {
            return hash_value(p.distance);
        } else if constexpr (std::is_same_v<T, ZoomParams>) {
            return hash_value(p.amount);
        } else if constexpr (std::is_same_v<T, SmoothWipeParams>) {
            return hash_value(p.feather);
        } else if constexpr (std::is_same_v<T, CircleIrisParams>) {
            u64 h = hash_vec2(p.center);
            return hash_combine(h, hash_value(p.feather));
        } else if constexpr (std::is_same_v<T, FlashParams>) {
            return hash_color(p.color);
        } else if constexpr (std::is_same_v<T, ProceduralRemotionParams>) {
            u64 h = hash_value(p.seed);
            h = hash_combine(h, hash_color(p.inner_color));
            h = hash_combine(h, hash_color(p.mid_color));
            return hash_combine(h, hash_color(p.outer_color));
        } else if constexpr (std::is_same_v<T, RemotionParams>) {
            u64 h = hash_value(p.speed);
            h = hash_combine(h, hash_value(p.direction));
            return hash_combine(h, hash_value(p.angle));
        } else if constexpr (std::is_same_v<T, DipToBlackParams>) {
            return hash_value(p.dip_px);
        } else if constexpr (std::is_same_v<T, FlyUnderFadeParams>) {
            return hash_value(p.dip_px);
        } else {
            static_assert(!sizeof(T), "Add an explicit semantic transition-parameter hash");
        }
    }, params);
}

[[nodiscard]] u64 hash_layer_transition_spec(const LayerTransitionSpec& spec) {
    u64 h = hash_string(spec.transition_id);
    h = hash_combine(h, static_cast<u64>(spec.direction));
    h = hash_combine(h, hash_bytes(&spec.duration, sizeof(spec.duration)));
    h = hash_combine(h, hash_bytes(&spec.delay, sizeof(spec.delay)));
    h = hash_combine(h, static_cast<u64>(spec.easing));
    h = hash_combine(h, hash_layer_transition_parameters(spec.parameters));
    return h;
}

[[nodiscard]] u64 hash_layer_transition_spec_v2(const LayerTransitionSpec& spec) {
    core::hash::HashBuilder builder;
    const auto add_float = [&builder](auto value) {
        builder.add(value == 0.0 ? decltype(value){0} : value);
    };
    const auto add_color = [&](const Color& color) {
        add_float(color.r); add_float(color.g); add_float(color.b); add_float(color.a);
    };
    const auto add_vec2 = [&](const Vec2& value) {
        add_float(value.x); add_float(value.y);
    };

    builder.add("chronon.layer-transition.v2")
        .add(spec.transition_id)
        .add_enum(spec.direction);
    add_float(spec.duration);
    add_float(spec.delay);
    builder.add_enum(spec.easing).add(spec.parameters.index());
    std::visit([&](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, std::monostate> ||
                      std::is_same_v<T, CrossfadeParams> ||
                      std::is_same_v<T, WipeLinearParams>) {
            // These alternatives carry no additional parameter data.
        } else if constexpr (std::is_same_v<T, SlideParams>) {
            add_float(p.distance);
        } else if constexpr (std::is_same_v<T, ZoomParams>) {
            add_float(p.amount);
        } else if constexpr (std::is_same_v<T, SmoothWipeParams>) {
            add_float(p.feather);
        } else if constexpr (std::is_same_v<T, CircleIrisParams>) {
            add_vec2(p.center); add_float(p.feather);
        } else if constexpr (std::is_same_v<T, FlashParams>) {
            add_color(p.color);
        } else if constexpr (std::is_same_v<T, ProceduralRemotionParams>) {
            add_float(p.seed); add_color(p.inner_color);
            add_color(p.mid_color); add_color(p.outer_color);
        } else if constexpr (std::is_same_v<T, RemotionParams>) {
            add_float(p.speed); add_float(p.direction); add_float(p.angle);
        } else if constexpr (std::is_same_v<T, DipToBlackParams> ||
                             std::is_same_v<T, FlyUnderFadeParams>) {
            add_float(p.dip_px);
        } else {
            static_assert(!sizeof(T), "Add an explicit semantic transition-parameter hash");
        }
    }, spec.parameters);
    return builder.finish();
}

namespace {

u64 hash_render_node_content_impl(const RenderNode& n, bool semantic_v2) {
    u64 seed = semantic_v2
        ? hash_string("chronon.render-node.content.v2")
        : hash_bytes(n.name.data(), n.name.size());
    if (semantic_v2) seed = hash_combine(seed, hash_bytes(n.name.data(), n.name.size()));
    // Ignore world_transform for content-only hashing
    seed = hash_combine(seed, semantic_v2 ? hash_shape_v2(n.shape) : hash_shape(n.shape));
    seed = hash_combine(seed, hash_color(n.color));
    seed = hash_combine(seed, semantic_v2 ? hash_fill_v2(n.fill) : hash_fill(n.fill));
    seed = hash_combine(seed, hash_value(n.inline_mesh_material.has_value()));
    seed = hash_combine(seed, hash_string(n.inline_mesh_texture_asset));
    seed = hash_combine(seed, hash_value(n.inline_mesh_animated));
    seed = hash_combine(seed, hash_value(n.gpu_particle_bokeh));
    seed = hash_combine(seed, hash_string(n.gpu_particle_sprite_path));
    seed = hash_combine(seed, hash_value(n.gpu_particle_instances.size()));
    for (const auto& particle : n.gpu_particle_instances) {
        seed = hash_combine(seed, hash_vec2(particle.position));
        seed = hash_combine(seed, hash_color(particle.color));
        seed = hash_combine(seed, hash_value(particle.size));
        seed = hash_combine(seed, hash_value(particle.rotation));
        seed = hash_combine(seed, hash_value(particle.sprite_scale));
        seed = hash_combine(seed, hash_value(particle.z));
        seed = hash_combine(seed, hash_vec2(particle.velocity));
    }
    if (n.inline_mesh_material) {
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->diffuse, sizeof(float)));
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->specular, sizeof(float)));
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->shininess, sizeof(float)));
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->rim_intensity, sizeof(float)));
        seed = hash_combine(seed, hash_value(n.inline_mesh_material->base_color.has_value()));
        if (n.inline_mesh_material->base_color)
            seed = hash_combine(seed, hash_color(*n.inline_mesh_material->base_color));
    }
    seed = hash_combine(seed, hash_value(n.visible));
    seed = hash_combine(seed, hash_surface_policy(n.surface_policy));
    seed = hash_combine(seed, hash_transform_policy(n.transform_policy));
    return seed;
}

} // namespace

[[nodiscard]] u64 hash_render_node_content_only(const RenderNode& n) {
    return hash_render_node_content_impl(n, false);
}

[[nodiscard]] u64 hash_render_node_placement_only(const RenderNode& n) {
    return hash_transform(n.world_transform);
}

[[nodiscard]] u64 hash_render_node(const RenderNode& n) {
    u64 seed = hash_render_node_content_only(n);
    return hash_combine(seed, hash_render_node_placement_only(n));
}

[[nodiscard]] u64 hash_render_node_content_v2(const RenderNode& n) {
    return hash_render_node_content_impl(n, true);
}

[[nodiscard]] u64 hash_render_node_placement_v2(const RenderNode& n) {
    return hash_combine(
        hash_string("chronon.render-node.placement.v2"),
        hash_render_node_placement_only(n));
}

[[nodiscard]] u64 hash_render_node_v2(const RenderNode& n) {
    return hash_combine(
        hash_render_node_content_v2(n),
        hash_render_node_placement_v2(n));
}


} // namespace chronon3d::graph
