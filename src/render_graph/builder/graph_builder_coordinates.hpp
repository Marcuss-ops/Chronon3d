#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include <glm/glm.hpp>
#include <cmath>

namespace chronon3d::graph::detail {

// Forward declarations
inline bool layer_needs_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx);
inline bool has_custom_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx);
inline bool is_implicit_2d_centering_only(const LayerGraphItem& item, const RenderGraphContext& ctx);

inline Mat4 implicit_canvas_center_matrix(const RenderGraphContext& ctx) {
    return math::translate(Vec3(
        ctx.width * 0.5f,
        ctx.height * 0.5f,
        0.0f
    ));
}

inline bool matrix_near(const Mat4& a, const Mat4& b, f32 eps = 1e-4f) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::abs(a[i][j] - b[i][j]) > eps) {
                return false;
            }
        }
    }
    return true;
}

/// Determine if a layer should be rendered in centered mode.
inline bool should_use_centered_rendering(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!ctx.modular_coordinates) {
        return (item.layer && item.layer->is_3d);
    }
    if (!item.layer) return true;
    bool use_local = layer_needs_render_transform(item, ctx) && !item.native_3d;
    return use_local;
}

/// True only when item.transform is exactly the automatic 2D centering transform.
/// This must NOT count as a user/custom transform.
inline bool is_implicit_2d_centering_only(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!ctx.modular_coordinates) return false;
    if (!item.layer) return false;
    if (item.projected) return false;
    if (item.native_3d) return false;
    if (item.layer->is_3d) return false;
    if (item.layer->kind != LayerKind::Normal) return false;
    if (!item.transform.any()) return false;

    return matrix_near(item.transform.to_mat4(), implicit_canvas_center_matrix(ctx));
}

/// True only for real user/hierarchy transform, not default canvas centering.
inline bool has_custom_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!item.transform.any()) return false;
    if (is_implicit_2d_centering_only(item, ctx)) return false;
    return true;
}

/// Shared decision: should this layer get a TransformNode / local sub-pipeline?
inline bool layer_needs_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!item.layer) return false;

    return item.projected
        || item.layer->kind == LayerKind::Precomp
        || item.layer->kind == LayerKind::Video
        || has_custom_render_transform(item, ctx);
}

/// SourceNode already applies canvas_center when centered=true.
/// So if item.world_matrix contains only implicit canvas centering,
/// strip it before passing matrix_override.
inline Mat4 source_space_world_matrix(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    Mat4 world = item.world_matrix;

    if (is_implicit_2d_centering_only(item, ctx) && should_use_centered_rendering(item, ctx)) {
        world = glm::inverse(implicit_canvas_center_matrix(ctx)) * world;
    }

    return world;
}

inline Transform calculate_centered_transform(const Transform& t, const RenderGraphContext&) {
    return t;
}

} // namespace chronon3d::graph::detail
