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
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "../nodes/detail/raster_surface.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace chronon3d::graph::detail {

[[nodiscard]] bool is_native_3d_layer(const Layer& layer);

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

    /// Visibility is resolved together with projection. A layer that is
    /// behind the camera (or explicitly hidden) is not a valid render input.
    /// Callers must use this bit instead of repeating projection checks.
    bool visible{false};

    Mat4 source_matrix{1.0f};
    Mat4 world_matrix{1.0f};
    Mat4 render_matrix{1.0f};
    Mat4 projection_matrix{1.0f};

    f32 opacity{1.0f};
    bool requires_transform_node{false};
    bool applies_camera_in_processor{false};
    bool defer_camera_projection{false};

    std::optional<raster::BBox> projected_bbox;
    Vec2 surface_size{0.0f, 0.0f};
    Vec2 surface_origin{0.0f, 0.0f};
};

/// Resolve the render-stage placement for a source payload that has already
/// been lowered by the graph builder. This is the node/executor counterpart
/// of `evaluate_layer_placement`: it owns the final SSAA, canvas-centre and
/// camera-projection composition so `predicted_bbox()` and `execute()` cannot
/// drift apart. `source_matrix` is the builder/refresh payload, not an
/// authored transform to be re-derived here.
[[nodiscard]] inline std::optional<EvaluatedLayerPlacement> evaluate_source_payload_placement(
    const Mat4& source_matrix,
    f32 opacity,
    const RenderGraphContext& ctx,
    bool apply_camera_projection,
    bool defer_camera_projection = false,
    bool native_3d = false,
    std::string_view node_name = {},
    const char* stage = nullptr,
    std::size_t item_index = static_cast<std::size_t>(-1),
    bool exclude_from_2_5d_projection = false)
{
    EvaluatedLayerPlacement result;
    result.world_matrix = source_matrix;
    result.source_matrix = source_matrix;
    result.opacity = opacity;
    result.defer_camera_projection = defer_camera_projection;
    result.applies_camera_in_processor =
        ctx.frame_input.has_camera_2_5d && native_3d;

    const bool project_camera =
        apply_camera_projection &&
        !defer_camera_projection &&
        !native_3d &&
        !exclude_from_2_5d_projection &&
        ctx.frame_input.has_camera_2_5d;

    if (project_camera) {
        const auto projected = project_to_camera_space(
            source_matrix,
            opacity,
            ctx,
            std::string(node_name),
            stage,
            item_index);
        if (!projected) {
            result.visible = false;
            result.space = EvaluatedCoordinateSpace::CameraProjected;
            return std::nullopt;
        }
        result.visible = true;
        result.space = EvaluatedCoordinateSpace::CameraProjected;
        result.render_matrix = *projected;
        result.requires_transform_node = true;
        return result;
    }

    result.visible = true;
    result.space = native_3d
        ? EvaluatedCoordinateSpace::Native3D
        : (defer_camera_projection
            ? EvaluatedCoordinateSpace::CameraProjected
            : EvaluatedCoordinateSpace::Canvas);
    const Mat4 ssaa_scale = glm::scale(
        Mat4(1.0f),
        Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    result.render_matrix = ssaa_scale * source_matrix;
    return result;
}

/// Return the optional matrix override shared by fresh and refreshed root
/// source nodes. Root shapes are authored in top-left canvas coordinates;
/// modular line sources retain the historical canvas-center bake.
[[nodiscard]] inline std::optional<Mat4> root_source_matrix_override(
    const RenderNode& node,
    const RenderGraphContext& ctx)
{
    if (ctx.policy.modular_coordinates && node.shape.type() == ShapeType::Line) {
        return implicit_canvas_center_matrix(ctx);
    }
    return std::nullopt;
}

/// Evaluate a root source through the same source-payload resolver used by
/// SourceNode::predicted_bbox(), execute(), builder and refresh. This keeps
/// dirty/root-bbox analysis from rebuilding SSAA, camera and centering rules.
[[nodiscard]] inline std::optional<EvaluatedLayerPlacement>
 evaluate_root_source_placement(
    const RenderNode& node,
    const RenderGraphContext& ctx,
    std::string_view node_name = {},
    const char* stage = nullptr)
{
    const auto override_matrix = root_source_matrix_override(node, ctx);
    const Mat4 source_matrix = override_matrix.value_or(
        node.world_transform.to_mat4());
    return evaluate_source_payload_placement(
        source_matrix,
        node.world_transform.opacity,
        ctx,
        true,
        false,
        false,
        node_name,
        stage);
}

/// Convert a local/node bbox through a canonical placement matrix into
/// canvas pixels. This is kept beside the placement resolver so projected
/// bbox math cannot drift between build, refresh, and dirty analysis.
[[nodiscard]] inline raster::BBox project_bbox_to_canvas(
    const raster::BBox& bbox,
    const Mat4& model,
    const RenderGraphContext& ctx)
{
    const Mat4 canvas = implicit_canvas_center_matrix(ctx);
    const Mat4 pixel_model = canvas * model * glm::inverse(canvas);
    const Vec4 corners[4] = {
        pixel_model * Vec4(static_cast<f32>(bbox.x0), static_cast<f32>(bbox.y0), 0.0f, 1.0f),
        pixel_model * Vec4(static_cast<f32>(bbox.x1), static_cast<f32>(bbox.y0), 0.0f, 1.0f),
        pixel_model * Vec4(static_cast<f32>(bbox.x1), static_cast<f32>(bbox.y1), 0.0f, 1.0f),
        pixel_model * Vec4(static_cast<f32>(bbox.x0), static_cast<f32>(bbox.y1), 0.0f, 1.0f),
    };
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();
    for (const auto& corner : corners) {
        if (std::abs(corner.w) < 1e-6f) continue;
        const f32 x = corner.x / corner.w;
        const f32 y = corner.y / corner.w;
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }
    if (min_x > max_x || min_y > max_y) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }
    return raster::BBox{
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)),
        static_cast<i32>(std::ceil(max_y)),
    };
}

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
    result.surface_size = item.projected_surface_size;
    result.surface_origin = item.projected_surface_origin;
    result.opacity = item.transform.opacity;
    result.visible = item.visible && item.layer && item.layer->visible;
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
        // Camera space is Y-up while raster surfaces are stored top-down.
        // Convert the local surface basis exactly once before the camera
        // homography reaches TransformNode; otherwise every projected text
        // surface is vertically mirrored at zero rotation.
        const Mat4 surface_y_down = glm::scale(
            Mat4(1.0f), Vec3(1.0f, -1.0f, 1.0f));
        // TextRunNode + composite_text_run already translate the glyph paint
        // by `-surface_origin` and then add the same raster offset, leaving
        // the producer framebuffer in local [0,size) coordinates. Reapplying
        // the origin here would shift the projected surface twice (the T03
        // pivot drift was approximately the tight-surface origin). The
        // consumer therefore owns only the basis conversion and projection.
        result.render_matrix =
            result.projection_matrix * surface_y_down;
        result.surface_size = item.projected_surface_size;
        result.surface_origin = item.projected_surface_origin;
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
    if (!ctx.policy.modular_coordinates &&
        (should_use_centered_rendering(item, ctx) || item.projected) &&
        !(item.layer && item.layer->uses_2_5d_projection)) {
        result.source_matrix = implicit_canvas_center_matrix(ctx) * result.source_matrix;
    }
    result.render_matrix = result.source_matrix;
    return result;
}

/// Finalize a bbox that was measured with the resolver's render matrix.
/// The input is already in the node's execution/render space; this helper
/// performs only the canonical visibility check and canvas clipping. It must
/// not apply `projection_matrix` again (doing so would double-project a bbox).
[[nodiscard]] inline std::optional<raster::BBox> resolve_execution_bbox(
    const EvaluatedLayerPlacement& placement,
    raster::BBox diagnostic_bbox,
    const RenderGraphContext& ctx)
{
    if (!placement.visible) {
        return raster::BBox{0, 0, 0, 0};
    }
    diagnostic_bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
    if (diagnostic_bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};
    }
    return diagnostic_bbox;
}

[[nodiscard]] inline EvaluatedLayerPlacement evaluate_layer_placement(
    const LayerGraphItem& item,
    const RenderGraphContext& ctx,
    const std::optional<raster::BBox>& diagnostic_bbox)
{
    auto result = evaluate_layer_placement(item, ctx);
    if (diagnostic_bbox) {
            result.projected_bbox = resolve_execution_bbox(
            result, *diagnostic_bbox, ctx);
    }
    return result;
}

/// Canonical source placement for one regular render node. TextRun has
/// additional authored-placement rules and continues to use its dedicated
/// resolver, but ordinary SourceNode/MultiSourceNode build and refresh paths
/// share this exact matrix and opacity composition.
struct EvaluatedSourcePlacement {
    EvaluatedLayerPlacement layer;
    Mat4 matrix{1.0f};
    f32 opacity{1.0f};
    bool use_local{false};
};

[[nodiscard]] inline EvaluatedSourcePlacement evaluate_source_placement(
    const LayerGraphItem& item,
    const RenderNode& node,
    const RenderGraphContext& ctx)
{
    EvaluatedSourcePlacement result{
        .layer = evaluate_layer_placement(item, ctx),
        .matrix = Mat4(1.0f),
        .opacity = node.world_transform.opacity,
        .use_local = false,
    };
    result.use_local = result.layer.space == EvaluatedCoordinateSpace::Local;
    const Mat4 node_matrix = node.world_transform.to_mat4();
    result.matrix = result.use_local
        ? node_matrix
        : result.layer.source_matrix * node_matrix;

    // TextRun placement is already resolved by LayerBuilder and consumed by
    // its dedicated source path. Regular source nodes retain the layer/node
    // composition here; no text placement kind is interpreted in this shared
    // evaluator.
    if (!result.use_local) {
        result.matrix = result.layer.source_matrix * node_matrix;
    }
    result.opacity = (result.use_local || result.layer.defer_camera_projection)
        ? node.world_transform.opacity
        : result.layer.opacity * node.world_transform.opacity;
    return result;
}

/// Apply source-payload compatibility baking after canonical placement.
/// This final step is shared by fresh graph construction and refresh so a
/// pinned fullscreen rectangle cannot drift between the two paths.
[[nodiscard]] inline Mat4 finalize_source_placement_matrix(
    const EvaluatedSourcePlacement& placement,
    const LayerGraphItem& item,
    const RenderNode& node,
    const RenderGraphContext& ctx)
{
    Mat4 matrix = placement.matrix;
    if (ctx.policy.modular_coordinates &&
        is_pinned_full_canvas_rect(item, node, ctx)) {
        matrix = implicit_canvas_center_matrix(ctx) * matrix;
    }
    return matrix;
}

/// Build the canonical intermediate layer item used by graph construction,
/// refresh, dirty-bbox collection, and matte sub-pipelines. Projection is
/// evaluated exactly once here; callers no longer need to reconstruct
/// `project_layer_2_5d()` or decide how native 3D owns the camera.
[[nodiscard]] inline LayerGraphItem resolve_layer_graph_item(
    const ResolvedLayer& resolved_layer,
    const RenderGraphContext& ctx,
    bool is_static = false)
{
    const Layer& layer = *resolved_layer.layer;
    const bool native_3d = is_native_3d_layer(layer);

    Vec2 projected_surface_size{0.0f, 0.0f};
    Vec2 projected_surface_origin{0.0f, 0.0f};
    bool has_projected_surface = false;
    for (const auto& node : layer.nodes) {
        if (node.shape.type() != ShapeType::TextRun) continue;
        const auto shape = node.shape.text_run_shape_handle().value;
        if (!shape) continue;
        const auto geometry = compute_tight_text_surface_geometry(*shape);
        if (!geometry) continue;

        // A projected layer may contain more than one text producer. Union
        // their local surface rectangles instead of letting the first node
        // define the projection for every sibling. This keeps the contract
        // reusable for future image/SVG producers that contribute bounds to
        // the same projected surface.
        const Vec2 geometry_max = geometry->origin + geometry->content_size;
        if (!has_projected_surface) {
            projected_surface_origin = geometry->origin;
            projected_surface_size = geometry->content_size;
            has_projected_surface = true;
            continue;
        }
        const Vec2 union_min{
            std::min(projected_surface_origin.x, geometry->origin.x),
            std::min(projected_surface_origin.y, geometry->origin.y)};
        const Vec2 union_max{
            std::max(projected_surface_origin.x + projected_surface_size.x, geometry_max.x),
            std::max(projected_surface_origin.y + projected_surface_size.y, geometry_max.y)};
        projected_surface_origin = union_min;
        projected_surface_size = union_max - union_min;
    }

    LayerGraphItem item{
        .layer = resolved_layer.layer,
        .transform = resolved_layer.world_transform,
        .world_matrix = resolved_layer.world_matrix,
        .depth = 0.0f,
        .world_z = resolved_layer.world_transform.position.z,
        .projected = false,
        .native_3d = native_3d,
        .visible = layer.visible,
        .projected_surface_size = projected_surface_size,
        .projected_surface_origin = projected_surface_origin,
        .insertion_index = resolved_layer.insertion_index,
        .matte_node = k_invalid_node,
        .is_static = is_static,
    };

    // Screen-space layers are already expressed in framebuffer coordinates;
    // they must never enter the camera projection path.  In particular this
    // keeps static subtitle/watermark overlays out of the projected tight
    // surface branch, where their canvas-space origin would be interpreted as
    // a local surface origin and clipped at the left edge.
    if (ctx.frame_input.has_camera_2_5d && layer.uses_2_5d_projection &&
        !layer.screen_space) {
        const auto projected = project_layer_2_5d(
            resolved_layer.world_transform,
            resolved_layer.world_matrix,
            ctx.frame_input.camera_2_5d,
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height),
            ctx.policy.diagnostics_enabled,
            (projected_surface_size.x > 0.0f && projected_surface_size.y > 0.0f)
                ? projected_surface_size
                : Vec2{static_cast<f32>(ctx.frame_input.width),
                       static_cast<f32>(ctx.frame_input.height)},
            BackfaceMode::Hidden);
        item.projected = true;
        item.visible = item.visible && projected.visible;
        if (!projected.visible) {
            return item;
        }

        item.transform = projected.transform;
        item.depth = projected.depth;
        item.world_z = resolved_layer.world_transform.position.z;
        // Native 3D keeps camera ownership in the source processor.  Its
        // graph item therefore carries identity for transform/shadow users;
        // projected 2D owns the complete homography in the TransformNode.
        item.projection_matrix = native_3d
            ? Mat4(1.0f)
            : projected.projection_matrix;
    }

    return item;
}

} // namespace chronon3d::graph::detail
