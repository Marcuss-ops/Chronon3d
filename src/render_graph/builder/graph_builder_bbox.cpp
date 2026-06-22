#include "graph_builder_pipeline.hpp"

#include "graph_builder_coordinates.hpp"
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

raster::BBox project_bbox_to_canvas(const raster::BBox& bbox, const Mat4& model, const RenderGraphContext& ctx) {
    const Mat4 dst_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const Mat4 src_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    Vec4 corners[4] = {
        pixel_model * Vec4(static_cast<f32>(bbox.x0), static_cast<f32>(bbox.y0), 0.0f, 1.0f),
        pixel_model * Vec4(static_cast<f32>(bbox.x1), static_cast<f32>(bbox.y0), 0.0f, 1.0f),
        pixel_model * Vec4(static_cast<f32>(bbox.x1), static_cast<f32>(bbox.y1), 0.0f, 1.0f),
        pixel_model * Vec4(static_cast<f32>(bbox.x0), static_cast<f32>(bbox.y1), 0.0f, 1.0f)
    };

    f32 min_x = 1e10f;
    f32 max_x = -1e10f;
    f32 min_y = 1e10f;
    f32 max_y = -1e10f;
    for (auto& c : corners) {
        if (std::abs(c.w) < 1e-6f) continue;
        f32 px = c.x / c.w;
        f32 py = c.y / c.w;
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
    }

    if (min_x > max_x || min_y > max_y) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    return raster::BBox{
        .x0 = static_cast<i32>(std::floor(min_x)),
        .y0 = static_cast<i32>(std::floor(min_y)),
        .x1 = static_cast<i32>(std::ceil(max_x)),
        .y1 = static_cast<i32>(std::ceil(max_y))
    };
}

} // namespace

raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer) {
    const Layer& layer = *item.layer;

    if (layer.kind == LayerKind::Adjustment) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    if (layer.kind != LayerKind::Normal) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    if (!renderer) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const bool centered = should_use_centered_rendering(item, ctx);

    const bool layer_needs_transform = layer_needs_render_transform(item, ctx);
    const bool use_local = ctx.policy.modular_coordinates && layer_needs_transform && !item.native_3d;

    raster::BBox layer_bbox{
        .x0 = std::numeric_limits<i32>::max(),
        .y0 = std::numeric_limits<i32>::max(),
        .x1 = std::numeric_limits<i32>::min(),
        .y1 = std::numeric_limits<i32>::min()
    };

    const Mat4 item_source_world = use_local
        ? item.world_matrix
        : source_space_world_matrix(item, ctx);

    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;

        const Mat4 node_matrix = node.world_transform.to_mat4();
        Mat4 actual_world_matrix;
        if (item.projected) {
            actual_world_matrix = node_matrix;
        } else {
            const Mat4 layer_inv = layer.transform.any() ? glm::inverse(layer.transform.to_mat4()) : Mat4(1.0f);
            actual_world_matrix = layer.hierarchy_resolved
                ? (item_source_world * node_matrix)
                : (item_source_world * layer_inv * node_matrix);
        }

        Mat4 matrix;
        if (use_local) {
            Mat4 shape_matrix = glm::inverse(item.world_matrix) * actual_world_matrix;
            matrix = canvas_center * ssaa_scale * shape_matrix;
        } else {
            if (item.projected || centered) {
                matrix = canvas_center * ssaa_scale * actual_world_matrix;
            } else {
                matrix = ssaa_scale * actual_world_matrix;
            }
        }

        auto* processor = renderer->software_registry().get_shape(node.shape.type());
        if (!processor) {
            return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
        }

        f32 spread = 0.0f;
        if (node.shadow.enabled) spread = std::max(spread, node.shadow.radius);
        if (node.glow.enabled)   spread = std::max(spread, node.glow.radius);

        raster::BBox node_bbox = processor->compute_world_bbox(node.shape, matrix, spread);
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

    if (item.projected) {
        return project_bbox_to_canvas(layer_bbox, item.projection_matrix, ctx);
    }

    if (use_local) {
        Mat4 model = item.world_matrix;
        const bool centered_render = should_use_centered_rendering(item, ctx);
        if (centered_render) {
            Mat4 ssaa_world = item.world_matrix;
            ssaa_world[3][0] *= ctx.policy.ssaa_factor;
            ssaa_world[3][1] *= ctx.policy.ssaa_factor;
            ssaa_world[3][2] *= ctx.policy.ssaa_factor;
            model =
                glm::translate(Mat4(1.0f), Vec3(-ctx.frame_input.width * 0.5f, -ctx.frame_input.height * 0.5f, 0.0f)) *
                ssaa_world;
        }
        return project_bbox_to_canvas(layer_bbox, model, ctx);
    }

    return layer_bbox;
}

} // namespace chronon3d::graph::detail
