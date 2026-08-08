#include "graph_builder_pipeline.hpp"

#include "graph_builder_coordinates.hpp"
#include "evaluated_layer_placement.hpp"
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph::detail {

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

        const auto snapshot = renderer->software_registry().snapshot();
        const auto processor = snapshot->shape_shared(
            snapshot->shape_handle(node.shape.type()));
        if (!processor) {
            return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
        }

        const raster::BBox node_bbox =
            processor->compute_world_bbox(node.shape, matrix, 0.0f);
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

} // namespace chronon3d::graph::detail
