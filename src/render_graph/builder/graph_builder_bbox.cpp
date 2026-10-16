#include "graph_builder_pipeline.hpp"

#include "graph_builder_coordinates.hpp"
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include "evaluated_layer_placement.hpp"
#include "passes/graph_builder_source_pass.hpp"
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::detail {

namespace {

/// Per-visible-node world-bbox union for a resolved layer.  Requires the
/// pre-loop guards (layer kind, placement visibility, renderer/snapshot
/// availability) to have already run — see the two public entry points.
raster::BBox compute_layer_node_union_bbox(
    const LayerGraphItem& item,
    const RenderGraphContext& ctx,
    const renderer::ProcessorRegistrySnapshot& snapshot)
{
    const Layer& layer = *item.layer;
    const auto placement = evaluate_layer_placement(item, ctx);
    const Mat4 ssaa_scale = glm::scale(
        Mat4(1.0f),
        Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = implicit_canvas_center_matrix(ctx);
    const bool centered = should_use_centered_rendering(item, ctx);
    const bool use_local = placement.space == EvaluatedCoordinateSpace::Local;

    raster::BBox layer_bbox{
        std::numeric_limits<i32>::max(),
        std::numeric_limits<i32>::max(),
        std::numeric_limits<i32>::min(),
        std::numeric_limits<i32>::min(),
    };
    const Mat4 item_source_world = placement.source_matrix;

    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;

        const Mat4 node_matrix = node.world_transform.to_mat4();
        Mat4 actual_world_matrix;
        if (item.projected) {
            // The canonical projection matrix owns layer/camera placement.
            // Compute the source bbox in node-local coordinates, then project
            // the union exactly once below.
            actual_world_matrix = node_matrix;
        } else {
            const Mat4 layer_inv = layer.transform.any()
                ? glm::inverse(layer.transform.to_mat4())
                : Mat4(1.0f);
            actual_world_matrix = layer.hierarchy_resolved
                ? (item_source_world * node_matrix)
                : (item_source_world * layer_inv * node_matrix);
        }

        if (ctx.policy.modular_coordinates &&
            is_pinned_full_canvas_rect(item, node, ctx)) {
            actual_world_matrix = canvas_center * actual_world_matrix;
        }

        Mat4 matrix;
        if (item.projected) {
            matrix = canvas_center * ssaa_scale * actual_world_matrix;
        } else if (use_local) {
            const Mat4 shape_matrix = glm::inverse(item.world_matrix) * actual_world_matrix;
            matrix = canvas_center * ssaa_scale * shape_matrix;
        } else if (item.native_3d ||
                   (centered && !(ctx.policy.modular_coordinates && !use_local))) {
            matrix = canvas_center * ssaa_scale * actual_world_matrix;
        } else {
            matrix = ssaa_scale * actual_world_matrix;
        }

        const auto processor = snapshot.shape_shared(
            snapshot.shape_handle(node.shape.type()));
        if (!processor) {
            return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
        }

        const RenderNode materialized_node = detail::materialize_mesh_node(node, ctx);
        // Meshes must be perspective-projected like the software draw path;
        // the processor's raw model-space bbox misplaces them on screen.
        const raster::BBox node_bbox = [&]() -> raster::BBox {
            if (materialized_node.shape.type() == ShapeType::Mesh) {
                if (auto projected = projected_native_3d_bbox(
                        ctx, materialized_node, matrix, 0.0f)) {
                    return *projected;
                }
                return raster::BBox{0, 0, 0, 0};
            }
            return processor->compute_world_bbox(materialized_node.shape, matrix, 0.0f);
        }();
        if (!node_bbox.is_empty()) {
            layer_bbox.x0 = std::min(layer_bbox.x0, node_bbox.x0);
            layer_bbox.y0 = std::min(layer_bbox.y0, node_bbox.y0);
            layer_bbox.x1 = std::max(layer_bbox.x1, node_bbox.x1);
            layer_bbox.y1 = std::max(layer_bbox.y1, node_bbox.y1);
        }
    }

    if (layer_bbox.x0 > layer_bbox.x1 || layer_bbox.y0 > layer_bbox.y1) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    if (item.projected && !item.native_3d) {
        const auto projected_placement = evaluate_layer_placement(item, ctx, layer_bbox);
        return projected_placement.projected_bbox.value_or(
            raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height});
    }

    if (use_local) {
        return project_bbox_to_canvas(layer_bbox, placement.render_matrix, ctx);
    }

    return layer_bbox;
}

} // namespace

raster::BBox compute_layer_bbox(
    const LayerGraphItem& item,
    const RenderGraphContext& ctx,
    SoftwareRenderer* renderer)
{
    const Layer& layer = *item.layer;

    if (layer.kind == LayerKind::Adjustment || layer.kind != LayerKind::Normal) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    const auto placement = evaluate_layer_placement(item, ctx);
    if (!placement.visible) {
        return raster::BBox{0, 0, 0, 0};
    }

    if (!renderer) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    // One snapshot per layer (previously one per visible node).  Registry
    // content cannot change while this layer is being analyzed, so a single
    // capture keeps processor dispatch consistent for every node in the layer.
    const auto snapshot = renderer->software_registry().snapshot();
    return compute_layer_node_union_bbox(item, ctx, *snapshot);
}

raster::BBox compute_layer_bbox(
    const LayerGraphItem& item,
    const RenderGraphContext& ctx,
    const renderer::ProcessorRegistrySnapshot& processor_snapshot)
{
    const Layer& layer = *item.layer;

    if (layer.kind == LayerKind::Adjustment || layer.kind != LayerKind::Normal) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    const auto placement = evaluate_layer_placement(item, ctx);
    if (!placement.visible) {
        return raster::BBox{0, 0, 0, 0};
    }

    return compute_layer_node_union_bbox(item, ctx, processor_snapshot);
}

} // namespace chronon3d::graph::detail
