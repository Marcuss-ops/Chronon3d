#include "graph_builder_pipeline.hpp"

#include "utils/graph_builder_camera25d_sorter.hpp"
#include "graph_builder_layer_pipeline.hpp"
#include "graph_builder_bbox.hpp"
#include "graph_builder_static.hpp"
#include "graph_builder_internal.hpp"

#include <iostream>
#include <span>
#include <unordered_set>
#include <limits>
#include <queue>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "graph_builder_coordinates.hpp"
#include <chronon3d/core/telemetry/render_telemetry.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

RenderGraph build_graph(const Scene& scene, RenderGraphContext& ctx,
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

    // Build a lookup: layer name → ResolvedLayer
    std::unordered_map<std::string, const ResolvedLayer*> name_to_resolved;
    for (const auto& rl : resolved.layers) {
        name_to_resolved[std::string(rl.layer->name)] = &rl;
    }

    // Compute static layers
    std::unordered_map<std::string, bool> is_static_cache;
    compute_static_layers(resolved, is_static_cache);

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

        spdlog::info("DEBUG CULL: layer={} bbox=[{}, {}, {}, {}] is_culled={} reason={}", item.layer->name, bbox.x0, bbox.y0, bbox.x1, bbox.y1, is_culled, cull_reason);

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
                LayerGraphItem matte_item = make_item_for_matte_source(*it->second, ctx, cam25d, is_static_cache);
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
        const bool is_static_val = is_static_cache[std::string(layer.name)];

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
            const Mat4 projection_world_matrix = effective_transform.to_mat4();
            auto proj = project_layer_2_5d(
                effective_transform,
                projection_world_matrix,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height),
                ctx.diagnostics_enabled
            );
            if (proj.visible) {
                const Mat4 eff_proj = is_native_3d_layer(layer)
                    ? Mat4(1.0f)
                    : proj.projection_matrix;
                current_3d_bin.push_back(LayerGraphItem{
                    .layer             = resolved_layer.layer,
                    .transform         = proj.transform,
                    .world_matrix      = resolved_layer.world_matrix,
                    .projection_matrix = eff_proj,
                    .depth             = proj.depth,
                    .world_z           = resolved_layer.world_transform.position.z,
                    .projected         = true,
                    .native_3d         = is_native_3d_layer(layer),
                    .insertion_index   = resolved_layer.insertion_index,
                    .matte_node        = k_invalid_node,
                    .is_static         = is_static_val,
                });
            }
        } else {
            flush_3d_bin();
            append_item(LayerGraphItem{
                .layer           = resolved_layer.layer,
                .transform       = resolved_layer.world_transform,
                .world_matrix    = resolved_layer.world_matrix,
                .depth           = 0.0f,
                .world_z         = resolved_layer.world_transform.position.z,
                .projected       = false,
                .native_3d       = is_native_3d_layer(layer),
                .insertion_index = resolved_layer.insertion_index,
                .matte_node      = k_invalid_node,
                .is_static         = is_static_val,
            });
        }
    }

    flush_3d_bin();
    graph.set_output(current);

    // ── Early-exit analysis: mark nodes covered by full-frame opaque layers ──
    ctx.early_exit_skip.assign(graph.size(), false);
    {
        // Walk the composite chain from output downwards.
        // When a composite node's layer_input is full-frame and opaque,
        // all nodes in its background_input subtree are marked for skip.
        std::vector<GraphNodeId> stack;
        if (graph.has_output()) {
            stack.push_back(graph.output());
        }

        while (!stack.empty()) {
            GraphNodeId id = stack.back();
            stack.pop_back();

            if (id >= static_cast<GraphNodeId>(graph.size())) continue;
            if (ctx.early_exit_skip[id]) continue; // already marked as skip

            const auto& node = graph.node(id);
            if (node.kind() == RenderGraphNodeKind::Composite) {
                const auto& inputs = graph.inputs(id);
                if (inputs.size() == 2) {
                    GraphNodeId bg_id   = inputs[0];
                    GraphNodeId layer_id = inputs[1];

                    // Check if the layer covers the full frame with opacity
                    bool layer_fully_covers = false;
                    const auto& layer_node = graph.node(layer_id);
                    auto bbox = layer_node.predicted_bbox(ctx);

                    if (bbox) {
                        bool full_frame = bbox->x0 <= 0 && bbox->y0 <= 0 &&
                                          bbox->x1 >= ctx.width && bbox->y1 >= ctx.height;
                        if (full_frame) {
                            // Check opacity: non-effect, non-mask, frame-invariant nodes
                            auto policy = layer_node.cache_policy();
                            bool likely_opaque =
                                layer_node.kind() != RenderGraphNodeKind::Effect &&
                                layer_node.kind() != RenderGraphNodeKind::Mask &&
                                !layer_node.frame_dependent();

                            if (likely_opaque) {
                                layer_fully_covers = true;
                            }
                        }
                    }

                    if (layer_fully_covers) {
                        // Mark the entire background subtree for skip
                        std::queue<GraphNodeId> bg_queue;
                        std::unordered_set<GraphNodeId> visited;
                        bg_queue.push(bg_id);
                        while (!bg_queue.empty()) {
                            GraphNodeId bg = bg_queue.front();
                            bg_queue.pop();
                            if (visited.count(bg)) continue;
                            if (bg >= static_cast<GraphNodeId>(graph.size())) continue;
                            visited.insert(bg);
                            ctx.early_exit_skip[bg] = true;
                            for (GraphNodeId bg_in : graph.inputs(bg)) {
                                bg_queue.push(bg_in);
                            }
                        }
                    } else {
                        // Continue walking both branches
                        for (GraphNodeId in : graph.inputs(id)) {
                            stack.push_back(in);
                        }
                    }
                }
            }
            // For non-composite nodes, walk inputs
            else {
                for (GraphNodeId in : graph.inputs(id)) {
                    stack.push_back(in);
                }
            }
        }
    }

    return graph;
}

RenderGraph build_render_graph_pipeline(const Scene& scene, const RenderGraphContext& ctx) {
    auto mutable_ctx = ctx;
    const auto resolved = resolve_layers(scene, mutable_ctx);
    return build_graph(scene, mutable_ctx, resolved);
}

} // namespace chronon3d::graph::detail
