#include "graph_builder_pipeline.hpp"

#include "utils/graph_builder_camera25d_sorter.hpp"
#include "graph_builder_layer_pipeline.hpp"

#include <iostream>
#include <span>
#include <unordered_set>
#include <limits>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "graph_builder_coordinates.hpp"
#include <chronon3d/core/render_telemetry.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// Native-3D shapes handle their own screen-space projection internally.
// They must use an identity projection_matrix so TransformNode is a pass-through.
static bool is_native_3d_layer(const Layer& layer) {
    for (const auto& node : layer.nodes) {
        if (node.shape.type == ShapeType::FakeBox3D  ||
            node.shape.type == ShapeType::GridPlane) {
            return true;
        }
    }
    return false;
}

static raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer) {
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

        Mat4 matrix;
        if (use_local) {
            Mat4 shape_matrix = layer.hierarchy_resolved ? node.world_transform.to_mat4() 
                                                         : (glm::inverse(item.world_matrix) * node.world_transform.to_mat4());
            matrix = canvas_center * ssaa_scale * shape_matrix;
        } else {
            if (item.projected || centered) {
                matrix = canvas_center * ssaa_scale * node.world_transform.to_mat4();
            } else {
                matrix = ssaa_scale * node.world_transform.to_mat4();
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
    // (e.g. text without a bounding box enabled, or all nodes invisible).
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
    }

    return layer_bbox;
}

RenderGraph build_graph(const Scene& scene, const RenderGraphContext& ctx,
                        const LayerResolutionResult& resolved) {
    RenderGraph graph;
    GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());
    current = LayerPipelineBuilder::append_root_sources(graph, scene, ctx, current);

    const Camera2_5DRuntime& cam25d = resolved.camera.camera;

    // Collect names of layers used as matte sources — they are not rendered directly.
    std::unordered_set<std::string> matte_source_names;
    for (const auto& rl : resolved.layers) {
        const Layer& l = *rl.layer;
        if (l.track_matte.active()) {
            matte_source_names.insert(std::string(l.track_matte.source_layer));
        }
    }

    // Build a lookup: layer name → LayerGraphItem (2D path only; 3D resolved in the loop).
    // Used to find matte sources at graph-build time.
    std::unordered_map<std::string, const ResolvedLayer*> name_to_resolved;
    for (const auto& rl : resolved.layers) {
        name_to_resolved[std::string(rl.layer->name)] = &rl;
    }

    // Build a LayerGraphItem for a matte source layer.
    // If the source is a 3D layer and the camera is active, apply the full
    // 2.5D projection so the matte silhouette matches the projected geometry.
    auto make_item_for_matte_source = [&](const ResolvedLayer& rl) -> LayerGraphItem {
        if (cam25d.enabled && rl.layer->is_3d) {
            Transform effective_transform = rl.world_transform;
            if (!ctx.modular_coordinates) {
                effective_transform.position.x -= ctx.width * 0.5f;
                effective_transform.position.y -= ctx.height * 0.5f;
            }
            const Mat4 projection_world_matrix = effective_transform.to_mat4();
            auto proj = project_layer_2_5d(
                effective_transform,
                projection_world_matrix,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height)
            );
            if (proj.visible) {
                const Mat4 eff_proj = is_native_3d_layer(*rl.layer)
                    ? Mat4(1.0f)
                    : proj.projection_matrix;
                return LayerGraphItem{
                    .layer             = rl.layer,
                    .transform         = proj.transform,
                    .world_matrix      = rl.world_matrix,
                    .projection_matrix = eff_proj,
                    .depth             = proj.depth,
                    .world_z           = rl.world_transform.position.z,
                    .projected         = true,
                    .native_3d         = is_native_3d_layer(*rl.layer),
                    .insertion_index   = rl.insertion_index,
                };
            }
        }
        return LayerGraphItem{
            .layer           = rl.layer,
            .transform       = rl.world_transform,
            .world_matrix    = rl.world_matrix,
            .depth           = 0.0f,
            .world_z         = rl.world_transform.position.z,
            .projected       = false,
            .native_3d       = is_native_3d_layer(*rl.layer),
            .insertion_index = rl.insertion_index,
        };
    };

    auto append_item = [&](LayerGraphItem item,
                           std::span<const ShadowCasterInfo> casters = {}) {
        auto* renderer = dynamic_cast<SoftwareRenderer*>(ctx.backend);
        raster::BBox bbox = compute_layer_bbox(item, ctx, renderer);

        bool is_culled = false;
        std::string cull_reason = "";
        uint64_t saved_pixels = 0;

        if (bbox.is_empty()) {
            is_culled = true;
            cull_reason = "empty_bbox";
        } else {
            bool intersects = !(bbox.x1 <= 0 || bbox.x0 >= ctx.width ||
                                bbox.y1 <= 0 || bbox.y0 >= ctx.height);
            if (!intersects) {
                is_culled = true;
                cull_reason = "frustum_culled";
                saved_pixels = static_cast<uint64_t>(bbox.x1 - bbox.x0) * (bbox.y1 - bbox.y0);
            }
        }

        if (ctx.counters) {
            ctx.counters->layer_culling_tests.fetch_add(1, std::memory_order_relaxed);
            if (is_culled) {
                ctx.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
            } else {
                ctx.counters->layers_visible.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Record culling event
        telemetry::CullingTelemetryRecord cull_rec;
        cull_rec.frame_number = static_cast<int>(ctx.frame);
        cull_rec.layer_id = std::string(item.layer->name);
        cull_rec.visible = !is_culled;
        cull_rec.reason = cull_reason;
        cull_rec.bbox_x = static_cast<float>(bbox.x0);
        cull_rec.bbox_y = static_cast<float>(bbox.y0);
        cull_rec.bbox_w = static_cast<float>(bbox.x1 - bbox.x0);
        cull_rec.bbox_h = static_cast<float>(bbox.y1 - bbox.y0);
        cull_rec.visible_x = 0;
        cull_rec.visible_y = 0;
        cull_rec.visible_w = 0;
        cull_rec.visible_h = 0;
        if (!is_culled) {
            raster::BBox visible_bbox = bbox;
            visible_bbox.clip_to(ctx.width, ctx.height);
            cull_rec.visible_x = static_cast<float>(visible_bbox.x0);
            cull_rec.visible_y = static_cast<float>(visible_bbox.y0);
            cull_rec.visible_w = static_cast<float>(visible_bbox.x1 - visible_bbox.x0);
            cull_rec.visible_h = static_cast<float>(visible_bbox.y1 - visible_bbox.y0);
        }
        cull_rec.saved_pixels = saved_pixels;
        telemetry::record_culling_telemetry(std::move(cull_rec));

        if (is_culled) {
            return;
        }

        // Pre-build the matte sub-pipeline if this layer has a track matte.
        if (item.layer->track_matte.active()) {
            const std::string src_name(item.layer->track_matte.source_layer);
            auto it = name_to_resolved.find(src_name);
            if (it != name_to_resolved.end() && it->second->layer->active_at(ctx.frame)) {
                LayerGraphItem matte_item = make_item_for_matte_source(*it->second);
                item.matte_node = LayerPipelineBuilder::build_matte_sub_pipeline(graph, matte_item, ctx);
            }
        }
        LayerPipelineBuilder::append_layer_pipeline(graph, item, current, ctx, cam25d, casters);
    };

    std::vector<LayerGraphItem> current_3d_bin;
    auto flush_3d_bin = [&]() {
        if (current_3d_bin.empty()) return;
        Camera25DLayerSorter::sort(current_3d_bin);

        // Collect projected casters from this bin for shadow projection
        std::vector<ShadowCasterInfo> bin_casters;
        for (const auto& item : current_3d_bin) {
            if (item.layer->material.casts_shadows && item.projected && !item.native_3d) {
                bin_casters.push_back({
                    .layer             = item.layer,
                    .world_matrix      = item.world_matrix,
                    .projection_matrix = item.projection_matrix,
                    .world_z           = item.world_z,
                    .projected         = item.projected,
                });
            }
        }

        for (const auto& item : current_3d_bin) {
            append_item(item, bin_casters);
        }
        current_3d_bin.clear();
    };

    for (const auto& resolved_layer : resolved.layers) {
        const Layer& layer = *resolved_layer.layer;

        if (!layer.active_at(ctx.frame)) {
            continue;
        }

        // Matte source layers are consumed exclusively by TrackMatteNode.
        if (matte_source_names.count(std::string(layer.name))) {
            flush_3d_bin();
            continue;
        }

        if (layer.kind == LayerKind::Null) {
            flush_3d_bin();
            continue;
        }

        if (cam25d.enabled && layer.is_3d) {
            Transform effective_transform = resolved_layer.world_transform;
            if (!ctx.modular_coordinates) {
                effective_transform.position.x -= ctx.width * 0.5f;
                effective_transform.position.y -= ctx.height * 0.5f;
            }
            const Mat4 projection_world_matrix = effective_transform.to_mat4();
            auto proj = project_layer_2_5d(
                effective_transform,
                projection_world_matrix,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height)
            );

            if (proj.visible) {
                // Native-3D shapes (FakeBox3D, GridPlane) project
                // directly to screen coords in SourceNode. TransformNode must be
                // identity (pass-through) to avoid double-projection.
                const Mat4 eff_proj = is_native_3d_layer(layer)
                    ? Mat4(1.0f)
                    : proj.projection_matrix;

                current_3d_bin.push_back({
                    .layer = &layer,
                    .transform = proj.transform,
                    .world_matrix = resolved_layer.world_matrix,
                    .projection_matrix = eff_proj,
                    .depth = proj.depth,
                    .world_z = resolved_layer.world_transform.position.z,
                    .projected = true,
                    .native_3d = is_native_3d_layer(layer),
                    .insertion_index = resolved_layer.insertion_index
                });
            } else {
                if (ctx.counters) {
                    ctx.counters->layer_culling_tests.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
                }
                telemetry::CullingTelemetryRecord cull_rec;
                cull_rec.frame_number = static_cast<int>(ctx.frame);
                cull_rec.layer_id = std::string(layer.name);
                cull_rec.visible = false;
                cull_rec.reason = "camera_frustum_culled";
                cull_rec.bbox_x = 0; cull_rec.bbox_y = 0; cull_rec.bbox_w = 0; cull_rec.bbox_h = 0;
                cull_rec.visible_x = 0; cull_rec.visible_y = 0; cull_rec.visible_w = 0; cull_rec.visible_h = 0;
                cull_rec.saved_pixels = 0;
                telemetry::record_culling_telemetry(std::move(cull_rec));
            }
        } else {
            flush_3d_bin();
            append_item({
                .layer = &layer,
                .transform = resolved_layer.world_transform,
                .world_matrix = resolved_layer.world_matrix,
                .depth = 0.0f,
                .world_z = resolved_layer.world_transform.position.z,
                .projected = false,
                .native_3d = is_native_3d_layer(layer),
                .insertion_index = resolved_layer.insertion_index
            });
        }
    }

    flush_3d_bin();
    graph.set_output(current);
    return graph;
}

} // namespace chronon3d::graph::detail
