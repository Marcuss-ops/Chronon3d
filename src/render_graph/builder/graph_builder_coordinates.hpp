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
    // Canonical TextPlacement{CanvasCenter, offset} is already lowered to
    // the node's anchor-relative transform.  Text layers without a parent
    // transform must receive the canvas translation in both coordinate
    // policies; the non-modular path does not reach the modular branch below.
    if (item.layer && item.layer->kind == LayerKind::Text
        && !item.projected
        && !item.native_3d) {
        // A text layer with a parent transform keeps that parent for the
        // TransformNode; marking it centered here would strip the parent
        // before the local TextRun matrix is applied.  A parentless text
        // layer, on the other hand, needs the one canvas-center bake.
        return !item.transform.any();
    }
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
    // BUG 1 / TICKET-TEXT-XOFFSET-DOUBLE — Option A.  Text-kind
    // layers have internal centering semantics in the text layout
    // engine (HarfBuzz shapes glyphs within the text box; the layout
    // engine produces a canvas-centered bbox from the box position).
    // Returning false here skips the TICKET-104 strip+replay that
    // would double-shift by ~text-box-half-width on X, pushing
    // canvas-centered text off-canvas to x≈302 (658 px left of 960).
    // The resolver treats Text-kind as having a "custom" transform,
    // routing through the `use_local` path which returns
    // `node.world_transform.to_mat4()` (already in canvas coords).
    if (item.layer->kind == LayerKind::Text) return false;
    if (item.projected) return false;
    if (item.native_3d) return false;
    if (item.layer->uses_2_5d_projection) return false;
    // Only check the 4×4 matrix (position/rotation/scale/anchor).
    // Opacity is NOT part of to_mat4() and must not prevent canvas-center
    // detection — a layer can be canvas-centered AND have custom opacity.
    // Without this, l.opacity(non-1.0) would trigger use_local=true which
    // skips canvas center bake, pushing text off-screen.
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

    // The legacy/non-modular path already resolves the complete world matrix
    // into the source node. Adding a TransformNode there applies the parent
    // placement a second time. TransformNode is owned by the modular path;
    // keep the legacy path source-space only.
    if (!ctx.policy.modular_coordinates) return false;

    // TextRunNode rasterizes directly into a full-canvas surface.  A
    // TransformNode after it cannot recover glyphs that were clipped while
    // their local anchor was negative, so text placement must be resolved in
    // the source matrix exactly once.
    if (item.layer->kind == LayerKind::Text) return false;

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

// TextPlacement::Absolute is already expressed in canvas coordinates.  Text
// layers can nevertheless carry the automatic canvas-centre transform from
// their layer placement.  Remove that implicit parent before composing the
// text node, including when the layer is aggregated by MultiSourceNode.
inline Mat4 resolve_absolute_text_source_matrix(
    const LayerGraphItem& item,
    const RenderNode& node,
    const RenderGraphContext& ctx,
    Mat4 matrix) {
    if (node.shape.type() != ShapeType::TextRun) return matrix;

    const auto run_shape = node.shape.text_run_shape_handle().value;
    const bool absolute_placement =
        run_shape && run_shape->placement_kind == TextPlacementKind::Absolute;
    if (absolute_placement && matrix_near(
            item.transform.to_mat4(), implicit_canvas_center_matrix(ctx))) {
        matrix = glm::inverse(implicit_canvas_center_matrix(ctx)) * matrix;
    }
    return matrix;
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

// ═══════════════════════════════════════════════════════════════════════════
// resolve_text_run_placement — dedicated TextRun coordinate resolver
// ═══════════════════════════════════════════════════════════════════════════
//
// TextRun nodes use this single resolver instead of the scattered
// source_space_world_matrix() + is_implicit_2d_centering_only() +
// should_use_centered_rendering() + manual canvas-center bake chain.
// The resolver encapsulates ALL coordinate decisions:
//
//   - modular-coordinates local path (use_local)
//   - parent layer placement, including implicit canvas-center
//   - canvas-center bake for non-modular centered layers
//
// Opacity is handled separately (returned via the opacity output param)
// because TextRunNode takes it as a distinct optional.
//
// Other shape types (SourceNode, MultiSourceNode) continue to use the
// general-purpose helpers directly.

/// Resolve TextRun placement in a single call.
///
/// TextRun glyphs are laid out in box-local coordinates.  The RenderNode
/// transform contributes the text pin/anchor offset and the Layer transform
/// remains the parent-space placement.  Both must therefore be composed once.
/// Stripping an implicit canvas-center from the parent leaves the node's
/// `T(-anchor)` uncompensated (for a 1920×1080 centered box: -960,-540),
/// which rasterizes glyphs successfully but composites the ink off-canvas.
inline TextRunPlacement resolve_text_run_placement(
    const LayerGraphItem& item,
    const ::chronon3d::RenderNode& node,
    const RenderGraphContext& ctx,
    f32& out_opacity
) {
    const bool needs_xform = layer_needs_render_transform(item, ctx);
    const bool use_local = ctx.policy.modular_coordinates
                        && needs_xform && !item.native_3d;


    if (use_local) {
        // The downstream TransformNode handles the layer transform and layer
        // opacity.  TextRunNode receives only the node-local text transform,
        // matching the SourceNode local path and avoiding double application.
        out_opacity = node.world_transform.opacity;
        return TextRunPlacement{node.world_transform.to_mat4()};
    }

    // Non-local path: compose parent layer placement and node-local text
    // placement directly.  In particular, retain an implicit canvas-center
    // carried by item.world_matrix: it compensates the text box anchor offset
    // exactly once instead of leaving the final model at -width/2,-height/2.
    Mat4 parent_matrix = resolve_absolute_text_source_matrix(
        item, node, ctx, item.world_matrix);
    Mat4 matrix = parent_matrix * node.world_transform.to_mat4();
    out_opacity = item.transform.opacity * node.world_transform.opacity;

    // Projected/non-implicit centered paths may request centered rendering
    // without carrying canvas-center in item.world_matrix.  Bake it only for
    // those paths; implicit-centered layers already contain it above.
    if (should_use_centered_rendering(item, ctx) && !is_implicit_2d_centering_only(item, ctx)) {
        matrix = implicit_canvas_center_matrix(ctx) * matrix;
    }

    return TextRunPlacement{matrix};
}

} // namespace chronon3d::graph::detail
