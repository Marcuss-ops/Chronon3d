#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

[[nodiscard]] u64 hash_shape(const Shape& s) {
    u64 seed = hash_value(static_cast<int>(s.type()));
    switch (s.type()) {
        case ShapeType::Rect:
            seed = hash_combine(seed, hash_vec2(s.rect().size));
            seed = hash_combine(seed, hash_value(s.rect().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rect().stroke.color));
            seed = hash_combine(seed, hash_value(s.rect().stroke.width));
            seed = hash_combine(seed, hash_bytes(&s.rect().stroke.alignment, sizeof(StrokeAlignment)));
            seed = hash_combine(seed, hash_value(s.rect().stroke.gradient.has_value()));
            if (s.rect().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.rect().stroke.gradient));
            return seed;
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(s.rounded_rect().size));
            seed = hash_combine(seed, hash_bytes(&s.rounded_rect().radius, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.rounded_rect().corner_radius.has_value()));
            if (s.rounded_rect().corner_radius)
                seed = hash_combine(seed, hash_bytes(s.rounded_rect().corner_radius->data(), sizeof(f32) * 4));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rounded_rect().stroke.color));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.width));
            seed = hash_combine(seed, hash_bytes(&s.rounded_rect().stroke.alignment, sizeof(StrokeAlignment)));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.gradient.has_value()));
            if (s.rounded_rect().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.rounded_rect().stroke.gradient));
            return seed;
        case ShapeType::Circle:
            seed = hash_combine(seed, hash_bytes(&s.circle().radius, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.circle().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.circle().stroke.color));
            seed = hash_combine(seed, hash_value(s.circle().stroke.width));
            seed = hash_combine(seed, hash_bytes(&s.circle().stroke.alignment, sizeof(StrokeAlignment)));
            seed = hash_combine(seed, hash_value(s.circle().stroke.gradient.has_value()));
            if (s.circle().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.circle().stroke.gradient));
            return seed;
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(s.line().to));
            seed = hash_combine(seed, hash_bytes(&s.line().thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.line().stroke.trim_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.line().stroke.trim_end, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.line().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.line().stroke.color));
            seed = hash_combine(seed, hash_value(s.line().stroke.width));
            return hash_combine(seed, hash_bytes(&s.line().stroke.alignment, sizeof(StrokeAlignment)));
        case ShapeType::Path:
            seed = hash_combine(seed, hash_value(s.path().commands.size()));
            for (const auto& cmd : s.path().commands) {
                seed = hash_combine(seed, hash_value(static_cast<int>(cmd.type)));
                seed = hash_combine(seed, hash_vec2(cmd.p0));
                seed = hash_combine(seed, hash_vec2(cmd.p1));
                seed = hash_combine(seed, hash_vec2(cmd.p2));
            }
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.width, sizeof(f32)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path().stroke.cap)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path().stroke.join)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.trim_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.trim_end, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.taper_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.taper_end, sizeof(f32)));
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
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.cap, sizeof(s.path().stroke.cap)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.join, sizeof(s.path().stroke.join)));
            return seed;
        case ShapeType::Image:
            seed = hash_combine(seed, hash_bytes(s.image().path.data(), s.image().path.size()));
            seed = hash_combine(seed, hash_vec2(s.image().size));
            seed = hash_combine(seed, hash_bytes(&s.image().opacity, sizeof(f32)));
            return hash_combine(seed, hash_color(s.image().tint));
        case ShapeType::TiledImage:
            seed = hash_combine(seed, hash_bytes(s.tiled_image().image.path.data(), s.tiled_image().image.path.size()));
            seed = hash_combine(seed, hash_vec2(s.tiled_image().image.size));
            seed = hash_combine(seed, hash_bytes(&s.tiled_image().image.opacity, sizeof(f32)));
            return hash_combine(seed, hash_color(s.tiled_image().image.tint));
        case ShapeType::GridBackground:
            seed = hash_combine(seed, hash_vec2(s.grid_background().size));
            seed = hash_combine(seed, hash_vec2(s.grid_background().offset));
            seed = hash_combine(seed, hash_color(s.grid_background().bg_color));
            seed = hash_combine(seed, hash_color(s.grid_background().grid_color));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().spacing, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().minor_thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().major_thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().major_every, sizeof(i32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().centered, sizeof(bool)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().edge_fade, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().major_glow, sizeof(f32)));
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
            seed = hash_combine(seed, hash_bytes(&s.fake_box3d().depth, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.fake_box3d().color));
            seed = hash_combine(seed, hash_bytes(&s.fake_box3d().top_tint, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.fake_box3d().side_tint, sizeof(f32)));
        case ShapeType::GridPlane:
            seed = hash_combine(seed, hash_vec3(s.grid_plane().world_pos));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.grid_plane().axis)));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane().extent, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane().spacing, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.grid_plane().color));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane().fade_distance, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.grid_plane().fade_min_alpha, sizeof(f32)));
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
    return hash_combine(hash_string("chronon.shape.v2"), hash_shape(shape));
}


} // namespace chronon3d::graph
