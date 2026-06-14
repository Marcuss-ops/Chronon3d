#pragma once

#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <glm/glm.hpp>
#include <cmath>

namespace chronon3d::graph::detail {

// Forward declarations
inline bool layer_needs_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx);
inline bool has_custom_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx);
inline bool is_implicit_2d_centering_only(const LayerGraphItem& item, const RenderGraphContext& ctx);

inline Mat4 implicit_canvas_center_matrix(const RenderGraphContext& ctx) {
    return glm::translate(Mat4(1.0f), Vec3(
        ctx.frame.width * 0.5f,
        ctx.frame.height * 0.5f,
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
    if (!ctx.options.modular_coordinates) {
        return (item.layer && item.layer->uses_2_5d_projection);
    }
    return layer_needs_render_transform(item, ctx) && !item.native_3d;
}

/// True only when item.transform is exactly the automatic 2D centering transform.
/// This must NOT count as a user/custom transform.
inline bool is_implicit_2d_centering_only(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!ctx.options.modular_coordinates) return false;
    if (!item.layer) return false;
    if (item.projected) return false;
    if (item.native_3d) return false;
    if (item.layer->uses_2_5d_projection) return false;
    if (item.layer->kind != LayerKind::Normal) return false;
    if (!item.transform.any()) return false;
    if (item.transform.opacity != 1.0f) return false;

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

/// Strip the implicit canvas-center translation from a TransformNode matrix.
///
/// The SourceNode / MultiSourceNode already applies the canvas-center
/// translation for non-modular 2D layers (item_source_world = item.world_matrix
/// without centering stripping).  Without this, the TransformNode would
/// re-apply the same translation a second time, pushing the rendered
/// content outside the visible viewport (text disappears off-screen).
///
/// This helper consolidates the stripping logic that was previously
/// duplicated in graph_builder_layer_passes.cpp (build-path) and
/// scene_refresh.hpp (refresh-path), keeping the two paths in sync.
///
/// @param matrix        The candidate effective matrix for the TransformNode.
/// @param item          The resolved layer graph item.
/// @param ctx           The current render graph context.
/// @param tolerance     How close to (width/2, height/2) the translation
///                      components must be to qualify for stripping
///                      (default 2.0 px, matching the original inline code).
/// @return              A matrix with the translation components zeroed when
///                      the item is a non-3D Normal layer and the
///                      translation is within `tolerance` of canvas centre;
///                      otherwise the input matrix is returned unchanged.
inline Mat4 strip_implicit_canvas_centering(
    const Mat4& matrix,
    const LayerGraphItem& item,
    const RenderGraphContext& ctx,
    f32 tolerance = 2.0f
) {
    if (item.native_3d) return matrix;
    if (!item.layer || item.layer->kind != LayerKind::Normal) return matrix;

    const f32 cx = static_cast<f32>(ctx.frame.width) * 0.5f;
    const f32 cy = static_cast<f32>(ctx.frame.height) * 0.5f;

    if (std::abs(matrix[3][0] - cx) < tolerance &&
        std::abs(matrix[3][1] - cy) < tolerance) {
        Mat4 result = matrix;
        result[3][0] = 0.0f;
        result[3][1] = 0.0f;
        result[3][2] = 0.0f;
        return result;
    }
    return matrix;
}

inline Transform calculate_centered_transform(const Transform& t, const RenderGraphContext&) {
    return t;
}

} // namespace chronon3d::graph::detail
