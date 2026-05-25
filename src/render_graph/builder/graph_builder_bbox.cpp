#include "graph_builder_bbox.hpp"
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <limits>
#include <algorithm>
#include <cmath>

namespace chronon3d::graph::detail {

bool is_native_3d_layer(const Layer& layer) {
    for (const auto& node : layer.nodes) {
        if (node.shape.type == ShapeType::FakeBox3D  ||
            node.shape.type == ShapeType::GridPlane) {
            return true;
        }
    }
    return false;
}

raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer) {
    const Layer& layer = *item.layer;

    // Adjustment layers always cover the full canvas
    if (layer.kind == LayerKind::Adjustment) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    // Non-Normal layers (Precomp, Video, Image, Text, etc.) have complex internal
    // pipelines whose bounds we cannot reliably predict here — return full canvas.
    if (layer.kind != LayerKind::Normal) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    // No software renderer means no shape processors — cannot compute precise bbox.
    if (!renderer) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
    const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const bool centered = should_use_centered_rendering(item, ctx);

    const bool layer_needs_transform = item.projected
        || layer.kind == LayerKind::Precomp
        || layer.kind == LayerKind::Video
        || item.transform.any();
    const bool use_local = ctx.modular_coordinates && layer_needs_transform && !item.native_3d;

    raster::BBox layer_bbox{
        .x0 = std::numeric_limits<i32>::max(),
        .y0 = std::numeric_limits<i32>::max(),
        .x1 = std::numeric_limits<i32>::min(),
        .y1 = std::numeric_limits<i32>::min()
    };

    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;

        const Mat4 layer_inv = layer.transform.any() ? glm::inverse(layer.transform.to_mat4()) : Mat4(1.0f);
        const Mat4 node_matrix = node.world_transform.to_mat4();
        const Mat4 actual_world_matrix = layer.hierarchy_resolved
            ? (item.world_matrix * node_matrix)
            : (item.world_matrix * layer_inv * node_matrix);

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

        auto* processor = renderer->software_registry().get_shape(node.shape.type);
        if (!processor) {
            // Unknown shape type: conservative fallback — no cull
            return raster::BBox{0, 0, ctx.width, ctx.height};
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

    // If no node contributed a measurable bbox, fall back conservatively
    if (layer_bbox.x0 > layer_bbox.x1 || layer_bbox.y0 > layer_bbox.y1) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    if (item.projected) {
        const Mat4 model = item.projection_matrix;
        const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 src_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

        Vec4 corners[4] = {
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
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
            return raster::BBox{0, 0, ctx.width, ctx.height};
        }

        layer_bbox.x0 = static_cast<i32>(std::floor(min_x));
        layer_bbox.y0 = static_cast<i32>(std::floor(min_y));
        layer_bbox.x1 = static_cast<i32>(std::ceil(max_x));
        layer_bbox.y1 = static_cast<i32>(std::ceil(max_y));
    } else if (use_local) {
        Mat4 model = item.world_matrix;
        if (should_use_centered_rendering(item, ctx)) {
            model = math::translate(Vec3(-ctx.width * 0.5f, -ctx.height * 0.5f, 0.0f)) * model;
        }
        const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 src_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

        Vec4 corners[4] = {
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
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
            return raster::BBox{0, 0, ctx.width, ctx.height};
        }

        layer_bbox.x0 = static_cast<i32>(std::floor(min_x));
        layer_bbox.y0 = static_cast<i32>(std::floor(min_y));
        layer_bbox.x1 = static_cast<i32>(std::ceil(max_x));
        layer_bbox.y1 = static_cast<i32>(std::ceil(max_y));
    }

    return layer_bbox;
}

} // namespace chronon3d::graph::detail
