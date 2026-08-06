#pragma once

// ---------------------------------------------------------------------------
// evaluated_layer_placement.hpp
//
// Internal, build-tree-only contract for the evaluated placement of one
// resolved layer. This increment makes the placement result the shared
// decision point for initial graph construction and scene refresh.
//
// The resolver deliberately composes the conventions already established in
// graph_builder_coordinates.hpp:
//   - modular/local versus canvas placement;
//   - implicit canvas-centre handling;
//   - projected versus native 3D ownership;
//   - TransformNode and processor camera ownership.
//
// It is not part of the installed SDK.  Cache identity, dirty history,
// rasterisation, and diagnostics remain outside this contract.
// ---------------------------------------------------------------------------

#include "graph_builder_coordinates.hpp"

#include <optional>

namespace chronon3d::graph::detail {

/// Coordinate space of the matrix consumed by the next render stage.
enum class EvaluatedCoordinateSpace {
    /// Layer content remains in the local/modular path and requires a
    /// separate transform stage to reach the canvas.
    Local,
    /// Layer content is already expressed in canvas-space coordinates.
    Canvas,
    /// Camera projection has been evaluated into a TransformNode matrix.
    CameraProjected,
    /// The layer remains in native 3D space and the camera is applied by the
    /// source processor using RenderState::projection.
    Native3D,
};

/// Canonical placement decision for a resolved LayerGraphItem.
///
/// Matrix ownership is intentionally explicit:
///   - `world_matrix` is the authored/resolved hierarchical layer matrix;
///   - `source_matrix` is the matrix used as the source-stage placement;
///   - `projection_matrix` is the evaluated camera matrix when available;
///   - `render_matrix` is the matrix consumed by the next stage (the
///     projection matrix for projected 2D layers, otherwise source_matrix).
///
/// `projected_bbox` is reserved for the bbox increment.  Keeping the field in
/// this contract now prevents the bbox path from inventing a second placement
/// result when it is migrated.
struct EvaluatedLayerPlacement {
    EvaluatedCoordinateSpace space{EvaluatedCoordinateSpace::Canvas};

    Mat4 source_matrix{1.0f};
    Mat4 world_matrix{1.0f};
    Mat4 render_matrix{1.0f};
    Mat4 projection_matrix{1.0f};

    f32 opacity{1.0f};
    bool visible{false};
    bool requires_transform_node{false};
    bool applies_camera_in_processor{false};
    bool defer_camera_projection{false};

    std::optional<raster::BBox> projected_bbox;
};

/// Evaluate one LayerGraphItem using the repository's existing coordinate
/// conventions.  No graph node is created and no render state is mutated.
///
/// The function is intentionally pure with respect to the context: it only
/// reads frame dimensions, camera presence, policy, and the already-resolved
/// LayerGraphItem. A projected LayerGraphItem carries the projection matrix
/// produced by `project_layer_2_5d()`; the same result is consumed by build
/// and refresh paths.
[[nodiscard]] inline EvaluatedLayerPlacement evaluate_layer_placement(
    const LayerGraphItem& item,
    const RenderGraphContext& ctx)
{
    EvaluatedLayerPlacement result;
    result.world_matrix = item.world_matrix;
    result.projection_matrix = item.projection_matrix;
    result.opacity = item.transform.opacity;
    result.visible = item.layer && item.layer->visible;
    result.requires_transform_node = layer_needs_render_transform(item, ctx);
    result.defer_camera_projection = item.projected && !item.native_3d;
    result.applies_camera_in_processor =
        item.native_3d && ctx.frame_input.has_camera_2_5d;

    // Keep this branch structurally identical to the source-pass and refresh
    // formulas where compatibility baking remains outside the placement
    // result. In
    // particular, projected 2D layers retain only the implicit canvas origin
    // in their source stage; their complete homography belongs to the
    // TransformNode. Native 3D layers retain their source-space matrix and
    // let the processor consume the camera projection context.
    const bool use_local =
        ctx.policy.modular_coordinates &&
        result.requires_transform_node &&
        !item.native_3d &&
        !item.projected;

    if (item.projected && !item.native_3d) {
        result.space = EvaluatedCoordinateSpace::CameraProjected;
        result.source_matrix = implicit_canvas_center_matrix(ctx);
        result.render_matrix = result.projection_matrix;
        return result;
    }

    if (item.native_3d) {
        result.space = EvaluatedCoordinateSpace::Native3D;
        result.source_matrix = source_space_world_matrix(item, ctx);
        result.render_matrix = result.source_matrix;
        return result;
    }

    if (use_local) {
        result.space = EvaluatedCoordinateSpace::Local;
        result.source_matrix = item.world_matrix;
        if (should_use_centered_rendering(item, ctx)) {
            Mat4 ssaa_world = item.world_matrix;
            ssaa_world[3][0] *= ctx.policy.ssaa_factor;
            ssaa_world[3][1] *= ctx.policy.ssaa_factor;
            ssaa_world[3][2] *= ctx.policy.ssaa_factor;
            result.render_matrix = glm::translate(
                Mat4(1.0f),
                Vec3(-ctx.frame_input.width * 0.5f,
                     -ctx.frame_input.height * 0.5f,
                     0.0f)) * ssaa_world;
        } else {
            result.render_matrix = strip_implicit_canvas_centering(
                item.world_matrix, item, ctx);
        }
        return result;
    }

    result.space = EvaluatedCoordinateSpace::Canvas;
    result.source_matrix = source_space_world_matrix(item, ctx);
    result.render_matrix = result.source_matrix;
    return result;
}

} // namespace chronon3d::graph::detail
