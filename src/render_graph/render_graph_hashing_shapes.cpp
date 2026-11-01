#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

[[nodiscard]] u64 hash_shape_impl(const Shape& s, bool semantic_v2) {
    const auto hash_value = [semantic_v2]<typename T>(const T& value) -> u64 {
        if (!semantic_v2) return ::chronon3d::graph::hash_value(value);
        core::hash::HashBuilder builder;
        if constexpr (std::is_floating_point_v<T>) {
            builder.add(value == 0.0f ? T{0} : value);
        } else if constexpr (std::is_enum_v<T>) {
            builder.add_enum(value);
        } else {
            builder.add(value);
        }
        return builder.finish();
    };
    const auto hash_vec2 = [semantic_v2](const Vec2& value) -> u64 {
        if (!semantic_v2) return ::chronon3d::graph::hash_vec2(value);
        core::hash::HashBuilder builder;
        builder.add(value.x == 0.0f ? 0.0f : value.x)
               .add(value.y == 0.0f ? 0.0f : value.y);
        return builder.finish();
    };
    const auto hash_vec3 = [semantic_v2](const Vec3& value) -> u64 {
        if (!semantic_v2) return ::chronon3d::graph::hash_vec3(value);
        core::hash::HashBuilder builder;
        builder.add(value.x == 0.0f ? 0.0f : value.x)
               .add(value.y == 0.0f ? 0.0f : value.y)
               .add(value.z == 0.0f ? 0.0f : value.z);
        return builder.finish();
    };
    const auto hash_color = [semantic_v2](const Color& value) -> u64 {
        if (!semantic_v2) return ::chronon3d::graph::hash_color(value);
        core::hash::HashBuilder builder;
        builder.add(value.r == 0.0f ? 0.0f : value.r)
               .add(value.g == 0.0f ? 0.0f : value.g)
               .add(value.b == 0.0f ? 0.0f : value.b)
               .add(value.a == 0.0f ? 0.0f : value.a);
        return builder.finish();
    };
    const auto hash_bytes = [](const void* data, usize size) -> u64 {
        return ::chronon3d::graph::hash_bytes(data, size);
    };
    const auto hash_scalar = [semantic_v2, &hash_bytes, &hash_value]<typename T>(const T& value) -> u64 {
        return semantic_v2 ? hash_value(value) : hash_bytes(&value, sizeof(T));
    };
    const auto hash_gradient = [semantic_v2](const graphics::GradientDefinition& gradient) -> u64 {
        return semantic_v2 ? hash_gradient_v2(gradient) : ::chronon3d::graph::hash_gradient(gradient);
    };
    const auto hash_fill = [semantic_v2](const Fill& fill) -> u64 {
        return semantic_v2 ? hash_fill_v2(fill) : ::chronon3d::graph::hash_fill(fill);
    };

    u64 seed = semantic_v2
        ? hash_combine(hash_string("chronon.shape.v2"), hash_value(static_cast<int>(s.type())))
        : hash_value(static_cast<int>(s.type()));
    switch (s.type()) {
        case ShapeType::Rect:
            seed = hash_combine(seed, hash_vec2(s.rect().size));
            seed = hash_combine(seed, hash_value(s.rect().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rect().stroke.color));
            seed = hash_combine(seed, hash_value(s.rect().stroke.width));
            seed = hash_combine(seed, hash_scalar(s.rect().stroke.alignment));
            seed = hash_combine(seed, hash_value(s.rect().stroke.gradient.has_value()));
            if (s.rect().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.rect().stroke.gradient));
            return seed;
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(s.rounded_rect().size));
            seed = hash_combine(seed, hash_scalar(s.rounded_rect().radius));
            seed = hash_combine(seed, hash_value(s.rounded_rect().corner_radius.has_value()));
            if (s.rounded_rect().corner_radius) {
                if (semantic_v2) {
                    for (const auto radius : *s.rounded_rect().corner_radius)
                        seed = hash_combine(seed, hash_value(radius));
                } else {
                    seed = hash_combine(seed, hash_bytes(s.rounded_rect().corner_radius->data(), sizeof(f32) * 4));
                }
            }
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rounded_rect().stroke.color));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.width));
            seed = hash_combine(seed, hash_scalar(s.rounded_rect().stroke.alignment));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.gradient.has_value()));
            if (s.rounded_rect().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.rounded_rect().stroke.gradient));
            return seed;
        case ShapeType::Circle:
            seed = hash_combine(seed, hash_scalar(s.circle().radius));
            seed = hash_combine(seed, hash_value(s.circle().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.circle().stroke.color));
            seed = hash_combine(seed, hash_value(s.circle().stroke.width));
            seed = hash_combine(seed, hash_scalar(s.circle().stroke.alignment));
            seed = hash_combine(seed, hash_value(s.circle().stroke.gradient.has_value()));
            if (s.circle().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.circle().stroke.gradient));
            return seed;
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(s.line().to));
            seed = hash_combine(seed, hash_scalar(s.line().thickness));
            seed = hash_combine(seed, hash_scalar(s.line().stroke.trim_start));
            seed = hash_combine(seed, hash_scalar(s.line().stroke.trim_end));
            seed = hash_combine(seed, hash_value(s.line().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.line().stroke.color));
            seed = hash_combine(seed, hash_value(s.line().stroke.width));
            return hash_combine(seed, hash_scalar(s.line().stroke.alignment));
        case ShapeType::Path:
            seed = hash_combine(seed, hash_value(s.path().commands.size()));
            for (const auto& cmd : s.path().commands) {
                seed = hash_combine(seed, hash_value(static_cast<int>(cmd.type)));
                seed = hash_combine(seed, hash_vec2(cmd.p0));
                seed = hash_combine(seed, hash_vec2(cmd.p1));
                seed = hash_combine(seed, hash_vec2(cmd.p2));
            }
            seed = hash_combine(seed, hash_scalar(s.path().stroke.width));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path().stroke.cap)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path().stroke.join)));
            seed = hash_combine(seed, hash_scalar(s.path().stroke.trim_start));
            seed = hash_combine(seed, hash_scalar(s.path().stroke.trim_end));
            seed = hash_combine(seed, hash_scalar(s.path().stroke.taper_start));
            seed = hash_combine(seed, hash_scalar(s.path().stroke.taper_end));
            seed = hash_combine(seed, hash_value(s.path().closed));
            seed = hash_combine(seed, hash_fill(s.path().fill));
            seed = hash_combine(seed, hash_value(s.path().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.path().stroke.color));
            seed = hash_combine(seed, hash_value(s.path().stroke.dash_array.size()));
            for (const auto dash : s.path().stroke.dash_array)
                seed = hash_combine(seed, hash_value(dash));
            seed = hash_combine(seed, hash_value(s.path().stroke.dash_offset));
            seed = hash_combine(seed, hash_value(s.path().stroke.gradient.has_value()));
            if (s.path().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.path().stroke.gradient));
            seed = hash_combine(seed, hash_value(s.path().stroke.brush.has_value()));
            if (s.path().stroke.brush)
                seed = hash_combine(seed, hash_value(s.path().stroke.brush->fingerprint()));
            seed = hash_combine(seed, hash_value(s.path().stroke.trim_start));
            seed = hash_combine(seed, hash_value(s.path().stroke.trim_end));
            seed = hash_combine(seed, hash_scalar(s.path().stroke.cap));
            seed = hash_combine(seed, hash_scalar(s.path().stroke.join));
            return seed;
        case ShapeType::Image:
            seed = hash_combine(seed, semantic_v2 ? hash_string(s.image().path)
                : hash_bytes(s.image().path.data(), s.image().path.size()));
            seed = hash_combine(seed, hash_vec2(s.image().size));
            seed = hash_combine(seed, hash_scalar(s.image().opacity));
            return hash_combine(seed, hash_color(s.image().tint));
        case ShapeType::TiledImage:
            seed = hash_combine(seed, semantic_v2 ? hash_string(s.tiled_image().image.path)
                : hash_bytes(s.tiled_image().image.path.data(), s.tiled_image().image.path.size()));
            seed = hash_combine(seed, hash_vec2(s.tiled_image().image.size));
            seed = hash_combine(seed, hash_scalar(s.tiled_image().image.opacity));
            return hash_combine(seed, hash_color(s.tiled_image().image.tint));
        case ShapeType::GridBackground:
            seed = hash_combine(seed, hash_vec2(s.grid_background().size));
            seed = hash_combine(seed, hash_vec2(s.grid_background().offset));
            seed = hash_combine(seed, hash_color(s.grid_background().bg_color));
            seed = hash_combine(seed, hash_color(s.grid_background().grid_color));
            seed = hash_combine(seed, hash_scalar(s.grid_background().spacing));
            seed = hash_combine(seed, hash_scalar(s.grid_background().minor_thickness));
            seed = hash_combine(seed, hash_scalar(s.grid_background().major_thickness));
            seed = hash_combine(seed, hash_scalar(s.grid_background().major_every));
            seed = hash_combine(seed, hash_scalar(s.grid_background().centered));
            seed = hash_combine(seed, hash_scalar(s.grid_background().edge_fade));
            seed = hash_combine(seed, hash_scalar(s.grid_background().major_glow));
            return hash_combine(seed, hash_value(s.grid_background().style));
        case ShapeType::Mesh:
            seed = hash_combine(seed, hash_string(s.mesh_shape().reference.path()));
            seed = hash_combine(seed, hash_string(s.mesh_shape().reference.owner()));
            seed = hash_combine(seed, hash_value(s.mesh_shape().reference.required()));
            if (s.mesh_shape().prepared) {
                seed = hash_combine(seed, hash_string(
                    s.mesh_shape().prepared->identity.cache_key()));
            }
            if (s.mesh_shape().inline_mesh) {
                const auto& mesh = *s.mesh_shape().inline_mesh;
                seed = hash_combine(seed, hash_string(mesh.name()));
                seed = hash_combine(seed, hash_value(mesh.vertices().size()));
                for (const auto& vertex : mesh.vertices()) {
                    seed = hash_combine(seed, hash_vec3(vertex.position));
                    seed = hash_combine(seed, hash_vec3(vertex.normal));
                    seed = hash_combine(seed, hash_vec2(vertex.uv));
                }
                seed = hash_combine(seed, hash_value(mesh.indices().size()));
                for (const auto index : mesh.indices())
                    seed = hash_combine(seed, hash_value(index));
            }
            return seed;
        case ShapeType::FakeBox3D:
            seed = hash_combine(seed, hash_vec3(s.fake_box3d().world_pos));
            seed = hash_combine(seed, hash_vec2(s.fake_box3d().size));
            seed = hash_combine(seed, hash_scalar(s.fake_box3d().depth));
            seed = hash_combine(seed, hash_color(s.fake_box3d().color));
            seed = hash_combine(seed, hash_scalar(s.fake_box3d().top_tint));
            return hash_combine(seed, hash_scalar(s.fake_box3d().side_tint));
        case ShapeType::GridPlane:
            seed = hash_combine(seed, hash_vec3(s.grid_plane().world_pos));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.grid_plane().axis)));
            seed = hash_combine(seed, hash_scalar(s.grid_plane().extent));
            seed = hash_combine(seed, hash_scalar(s.grid_plane().spacing));
            seed = hash_combine(seed, hash_color(s.grid_plane().color));
            seed = hash_combine(seed, hash_scalar(s.grid_plane().fade_distance));
            return hash_combine(seed, hash_scalar(s.grid_plane().fade_min_alpha));
        case ShapeType::TextRun:
            if (s.text_run_shape_handle().value) {
                seed = hash_combine(seed, hash_text_run_shape(*s.text_run_shape_handle().value));
            }
            return seed;
        default:
            return seed;
    }
}

[[nodiscard]] u64 hash_shape_v2(const Shape& shape) {
    return hash_shape_impl(shape, true);
}

[[nodiscard]] u64 hash_shape(const Shape& shape) {
    return hash_shape_impl(shape, false);
}


} // namespace chronon3d::graph
