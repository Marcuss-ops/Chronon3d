#pragma once

#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <glm/glm.hpp>
#include <cmath>

namespace chronon3d::graph::detail {

// Forward declarations
inline bool layer_needs_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx);
inline bool has_custom_render_transform(const LayerGraphItem& item, const RenderGraphContext& ctx);
inline bool is_implicit_2d_centering_only(const LayerGraphItem& item, const RenderGraphContext& ctx);

inline Mat4 implicit_canvas_center_matrix(const RenderGraphContext& ctx) {
    return glm::translate(Mat4(1.0f), Vec3(
        ctx.frame_input.width * 0.5f,
        ctx.frame_input.height * 0.5f,
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
///
/// TICKET-104 — added the `is_implicit_2d_centering_only` check so the
/// `centered` flag is also true for canvas-center layers (`pin_to(Anchor::Center)`
/// on Normal/Shape/Text kinds).  Without this, `source_space_world_matrix`
/// refuses to strip the implicit canvas-center from `item.world_matrix`
/// (it requires BOTH `is_implicit_2d_centering_only` AND
/// `should_use_centered_rendering` to be true), so canvas-center layers
/// end up with double canvas-center in their final `m_matrix_override`:
///   item_source_world = item.world_matrix (no strip)
///   m_matrix_override = item.world_matrix * node.world_transform
/// For `pin_to(Anchor::Center)` layers, this is `translate(960, 540) *
/// translate(960, 540) = translate(1920, 1080)` — text goes off-canvas
/// at the bottom-right corner, executor's predicted_bbox clips to empty,
/// execute is skipped.  The fix: also return true for canvas-center
/// layers so the source pass strips the implicit canvas-center and the
/// node receives a canvas-center-stripped `m_matrix_override` that the
/// downstream `build_world_matrix` (TICKET-104 fix: skip `canvas_center`
/// when `matrix_override` is set) re-applies exactly once.
inline bool should_use_centered_rendering(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!ctx.policy.modular_coordinates) {
        return (item.layer && item.layer->uses_2_5d_projection);
    }
    return is_implicit_2d_centering_only(item, ctx)
        || (layer_needs_render_transform(item, ctx) && !item.native_3d);
}

/// True only when item.transform is exactly the automatic 2D centering transform.
/// This must NOT count as a user/custom transform.
///
/// TICKET-104 — the `LayerKind::Normal` restriction was removed: the
/// `matrix_near(item.transform.to_mat4(), implicit_canvas_center_matrix(ctx))`
/// check at the end is the semantic gate.  If a layer's transform is
/// exactly the canvas-center translation (regardless of kind — Normal,
/// Shape, Text, etc.), the source pass should strip the implicit
/// canvas-center before passing the matrix as `matrix_override`, so the
/// downstream node reapplies it exactly once.  Restricting to
/// `LayerKind::Normal` made Shape-kind layers (e.g. `bg_grid`) fall
/// through to the non-stripped path, producing a different matrix than
/// the Normal-kind TextRunNode for the same `pin_to(Anchor::Center)`
/// intent.  The kind check is a conservative gate that prevented the
/// parity fix; the `matrix_near` check is the actual semantic guard.
inline bool is_implicit_2d_centering_only(const LayerGraphItem& item, const RenderGraphContext& ctx) {
    if (!ctx.policy.modular_coordinates) return false;
    if (!item.layer) return false;
    if (item.projected) return false;
    if (item.native_3d) return false;
    if (item.layer->uses_2_5d_projection) return false;
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

    const f32 cx = static_cast<f32>(ctx.frame_input.width) * 0.5f;
    const f32 cy = static_cast<f32>(ctx.frame_input.height) * 0.5f;

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

/// TextRun placement resolver — bypasses all centering machinery.
///
/// TextRun items receive a pre-resolved `TextRunPlacement` from the graph
/// builder.  `build_world_matrix` only applies SSAA scaling on top — no
/// canvas-centre or centering-mode decisions happen downstream.  This
/// function computes the final placement directly from the layer item and
/// node transforms, WITHOUT going through `source_space_world_matrix`,
/// `is_implicit_2d_centering_only`, or `should_use_centered_rendering`.
///
/// The centering functions remain available for non-TextRun shapes
/// (SourceNode / MultiSourceNode regular items).
inline TextRunPlacement resolve_text_run_placement(
    const LayerGraphItem& item,
    const ::chronon3d::RenderNode& node,
    const RenderGraphContext& ctx
) {
    const bool needs_transform = layer_needs_render_transform(item, ctx);
    const bool use_local = ctx.policy.modular_coordinates && needs_transform && !item.native_3d;

    if (use_local) {
        return TextRunPlacement{node.world_transform.to_mat4()};
    }
    // Raw world matrix — no canvas-centre stripping.  TextRunNode's
    // `build_world_matrix` only applies SSAA and trusts the builder
    // has already resolved everything else.
    return TextRunPlacement{item.world_matrix * node.world_transform.to_mat4()};
}

} // namespace chronon3d::graph::detail
