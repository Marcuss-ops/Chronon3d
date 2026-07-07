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
//   - implicit canvas-center strip (for pin_to(Center) layers)
//   - canvas-center bake for non-modular centered layers
//
// Opacity is handled separately (returned via the opacity output param)
// because TextRunNode takes it as a distinct optional.
//
// Other shape types (SourceNode, MultiSourceNode) continue to use the
// general-purpose helpers directly.

/// Resolve TextRun placement in a single call.
///
/// Equivalent to the old sequence:
///   item_source_world = source_space_world_matrix(item, ctx)
///   run_matrix = item_source_world * node.world_transform.to_mat4()
///   if (!modular && centered) resolved = canvas_center * run_matrix
/// but encapsulated so the source pass's TextRun branch doesn't depend
/// on those helpers directly.
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
        out_opacity = node.world_transform.opacity;
        return TextRunPlacement{node.world_transform.to_mat4()};
    }

    // Non-local path: build item-level world matrix.
    // For implicit-centering-only layers, strip the canvas center
    // so it's applied exactly once (re-applied below if centered).
    Mat4 item_world = item.world_matrix;
    if (is_implicit_2d_centering_only(item, ctx)) {
        item_world = glm::inverse(implicit_canvas_center_matrix(ctx)) * item_world;
    }

    Mat4 matrix = item_world * node.world_transform.to_mat4();
    out_opacity = item.transform.opacity * node.world_transform.opacity;

    // Bake canvas center for centered layers.
    //
    // For non-modular: the source pass does NOT bake canvas center into
    // TextRun nodes (the TextRun branch returns early before the bake).
    // For modular: the source pass strips canvas center from item_world
    // but does not re-apply it (same reason — TextRun returns early).
    //
    // In BOTH cases, the resolver must provide the final canvas-center
    // translation so TextRunNode's draw_text_run renders at the correct
    // canvas position.  SourceNode and MultiSourceNode handle their own
    // canvas-center bake in the source pass; TextRunNode relies on us.
    if (should_use_centered_rendering(item, ctx)) {
        matrix = implicit_canvas_center_matrix(ctx) * matrix;
    }

    return TextRunPlacement{matrix};
}

} // namespace chronon3d::graph::detail
