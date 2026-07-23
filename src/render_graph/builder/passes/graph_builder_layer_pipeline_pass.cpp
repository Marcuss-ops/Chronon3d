#include "graph_builder_passes.hpp"
#include "../graph_builder_pipeline.hpp"
#include "../graph_builder_coordinates.hpp"
#include "graph_builder_layer_passes.hpp"
#include "graph_builder_lighting_passes.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/render_graph/nodes/clip_transition_node.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <span>
#include <string>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

namespace {

LayerGraphItem make_layer_graph_item(const ResolvedLayer& resolved_layer,
                                     const Camera2_5DRuntime& cam25d,
                                     const RenderGraphContext& rctx,
                                     bool is_static_val) {
    const Layer& layer = *resolved_layer.layer;
    if (cam25d.enabled && layer.uses_2_5d_projection) {
        Transform effective_transform = resolved_layer.world_transform;
        const Mat4 projection_world_matrix = effective_transform.to_mat4();
        auto proj = project_layer_2_5d(
            effective_transform, projection_world_matrix, cam25d,
            static_cast<f32>(rctx.frame_input.width), static_cast<f32>(rctx.frame_input.height),
            rctx.policy.diagnostics_enabled);
        if (proj.visible) {
            Vec3 projected_position = proj.transform.position;
            if (glm::length(Vec2(projected_position.x, projected_position.y)) < 1e-4f &&
                glm::length(Vec2(effective_transform.position.x, effective_transform.position.y)) > 1e-4f) {
                projected_position = effective_transform.position;
            }
            const Mat4 eff_proj = is_native_3d_layer(layer)
                ? Mat4(1.0f)
                : glm::translate(Mat4(1.0f), Vec3(projected_position.x, projected_position.y, 0.0f)) *
                    glm::scale(Mat4(1.0f), Vec3(proj.perspective_scale, proj.perspective_scale, 1.0f));
            return LayerGraphItem{
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
            };
        }
    }
    return LayerGraphItem{
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
    };
}

} // namespace

void LayerPipelinePass::run(GraphBuildContext& ctx) {
    auto& graph = *ctx.graph;
    auto& rctx  = *ctx.render_ctx;
    const auto& cam25d = ctx.cam25d;

    // Storage for sub-pipelines of layers referenced by clip transitions.
    std::unordered_map<std::string, GraphNodeId> clip_sub_pipelines;

    // ── append_item lambda ─────────────────────────────────────────────
    // Per-layer: culling, matte sub-pipeline, then full layer pipeline.
    auto append_item = [&](LayerGraphItem item,
                           std::span<const ShadowCasterInfo> casters = {}) {
        // 06 R3b — surface the typed SoftwareRenderer sidecar pointer
        // (when present) by static_cast from the bundle.  No
        // runtime RTTI on the backend ref is not used because the
        // caller pipeline already populates `sw_renderer_sidecar`
        // uniformly.
        auto* renderer =
            static_cast<chronon3d::SoftwareRenderer*>(rctx.services.sw_renderer_sidecar);
        (void)renderer;
        raster::BBox bbox = detail::compute_layer_bbox(item, rctx, nullptr);

        bool is_culled = false;
        std::string cull_reason;
        uint64_t saved_pixels = 0;

        if (bbox.is_empty()) {
            is_culled = true;
            cull_reason = "empty_bbox";
        } else {
            bool intersects = !(bbox.x1 <= 0 || bbox.x0 >= rctx.frame_input.width ||
                                bbox.y1 <= 0 || bbox.y0 >= rctx.frame_input.height);
            if (!intersects) {
                is_culled = true;
                cull_reason = "frustum_culled";
                saved_pixels = static_cast<uint64_t>(bbox.x1 - bbox.x0)
                             * (bbox.y1 - bbox.y0);
            }
        }

        if (rctx.node_exec.counters) {
            rctx.node_exec.counters->layer_culling_tests.fetch_add(1, std::memory_order_relaxed);
            if (is_culled) {
                rctx.node_exec.counters->layers_culled.fetch_add(1, std::memory_order_relaxed);
            } else {
                rctx.node_exec.counters->layers_visible.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Record culling telemetry
        telemetry::CullingTelemetryRecord cull_rec;
        cull_rec.frame_number = static_cast<int>(rctx.frame_input.frame);
        cull_rec.layer_id     = std::string(item.layer->name);
        cull_rec.visible      = !is_culled;
        cull_rec.reason       = cull_reason;
        cull_rec.bbox_x       = static_cast<float>(bbox.x0);
        cull_rec.bbox_y       = static_cast<float>(bbox.y0);
        cull_rec.bbox_w       = static_cast<float>(bbox.x1 - bbox.x0);
        cull_rec.bbox_h       = static_cast<float>(bbox.y1 - bbox.y0);
        cull_rec.visible_x    = 0;
        cull_rec.visible_y    = 0;
        cull_rec.visible_w    = 0;
        cull_rec.visible_h    = 0;
        if (!is_culled) {
            raster::BBox visible_bbox = bbox;
            visible_bbox.clip_to(rctx.frame_input.width, rctx.frame_input.height);
            cull_rec.visible_x = static_cast<float>(visible_bbox.x0);
            cull_rec.visible_y = static_cast<float>(visible_bbox.y0);
            cull_rec.visible_w = static_cast<float>(visible_bbox.x1 - visible_bbox.x0);
            cull_rec.visible_h = static_cast<float>(visible_bbox.y1 - visible_bbox.y0);
        }
        cull_rec.saved_pixels = saved_pixels;
        telemetry::record_culling_telemetry(std::move(cull_rec));

        if (is_culled) return;

        // Pre-build the matte sub-pipeline if this layer has a track matte.
        if (item.layer->track_matte.active()) {
            const std::string src_name(item.layer->track_matte.source_layer);
            auto it = ctx.name_to_resolved.find(src_name);
            if (it != ctx.name_to_resolved.end()
                && it->second->layer->active_at(rctx.frame_input.frame)) {
                LayerGraphItem matte_item = detail::make_item_for_matte_source(
                    *it->second, rctx, cam25d, ctx.is_static_cache);
                item.matte_node = detail::build_matte_sub_pipeline(
                    graph, matte_item, rctx);
            }
        }
        detail::append_layer_pipeline(
            graph, item, ctx.current_output, rctx, cam25d, casters,
            ctx.scene->depth_grade());
    };

    // ── flush_3d_bin lambda ────────────────────────────────────────────
    std::vector<LayerGraphItem> current_3d_bin;
    auto flush_3d_bin = [&]() {
        if (current_3d_bin.empty()) return;
        detail::sort_camera25d_layers(current_3d_bin);

        // Collect projected casters from this bin for shadow projection
        std::vector<ShadowCasterInfo> bin_casters;
        for (const auto& item : current_3d_bin) {
            if (item.layer->material().casts_shadows
                && item.projected && !item.native_3d) {
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

    // ── Helper to build a sub-pipeline for a named resolved layer ───────
    auto build_clip_sub_pipeline = [&](const ResolvedLayer& rl) -> GraphNodeId {
        const std::string name(rl.layer->name);
        auto it = clip_sub_pipelines.find(name);
        if (it != clip_sub_pipelines.end()) {
            return it->second;
        }

        const bool is_static_val = ctx.is_static_cache.at(name);
        LayerGraphItem item = make_layer_graph_item(rl, cam25d, rctx, is_static_val);

        // Resolve track-matte source for this sub-pipeline if needed.
        if (rl.layer->track_matte.active()) {
            const std::string src_name(rl.layer->track_matte.source_layer);
            auto matte_it = ctx.name_to_resolved.find(src_name);
            if (matte_it != ctx.name_to_resolved.end()
                && matte_it->second->layer->active_at(rctx.frame_input.frame)) {
                LayerGraphItem matte_item = detail::make_item_for_matte_source(
                    *matte_it->second, rctx, cam25d, ctx.is_static_cache);
                item.matte_node = detail::build_matte_sub_pipeline(
                    graph, matte_item, rctx);
            }
        }

        BuilderContext node_ctx{
            .layer_id = name,
            .opacity_evaluator = [opacity = rl.layer->anim_transform.opacity](const RenderFrameInfo& info) -> float {
                return opacity.evaluate(info.sample_time);
            }
        };

        GraphNodeId out = detail::build_layer_output_node(
            graph, item, rctx, cam25d, std::span<const ShadowCasterInfo>{},
            ctx.scene->depth_grade(), node_ctx);
        clip_sub_pipelines[name] = out;
        return out;
    };

    // ── Main layer iteration ───────────────────────────────────────────
    for (const auto& resolved_layer : ctx.resolved.layers) {
        const Layer& layer = *resolved_layer.layer;
        const std::string layer_name(layer.name);
        const bool is_static_val =
            ctx.is_static_cache.at(layer_name);

        if (!layer.active_at(rctx.frame_input.frame)) continue;

        // Matte source layers are consumed exclusively by TrackMatteNode.
        if (ctx.matte_source_names.count(layer_name)) {
            flush_3d_bin();
            continue;
        }

        if (layer.kind == LayerKind::Null) {
            flush_3d_bin();
            continue;
        }

        // If this layer is the target of a clip transition, build the
        // sub-pipelines for both source and target, wire the
        // ClipTransitionNode, and composite it onto the current output.
        auto ct_it = ctx.clip_transition_by_target.find(layer_name);
        if (ct_it != ctx.clip_transition_by_target.end()) {
            flush_3d_bin();

            const auto& ct = ct_it->second;
            GraphNodeId a_out = k_invalid_node;
            GraphNodeId b_out = k_invalid_node;

            auto a_it = ctx.name_to_resolved.find(std::string(ct.layer_a));
            auto b_it = ctx.name_to_resolved.find(layer_name);
            if (a_it != ctx.name_to_resolved.end()) {
                a_out = build_clip_sub_pipeline(*a_it->second);
            }
            if (b_it != ctx.name_to_resolved.end()) {
                b_out = build_clip_sub_pipeline(*b_it->second);
            }

            if (a_out == k_invalid_node || b_out == k_invalid_node) {
                // One of the clip transition inputs could not be built.
                // Fall back to compositing the target layer normally so
                // the scene remains renderable.
                if (b_out != k_invalid_node) {
                    BuilderContext node_ctx{.layer_id = layer_name};
                    const bool is_static = layer.cache_static || is_static_val;
                    detail::append_composite_pass(
                        graph, ctx.current_output, b_out, layer, is_static,
                        rctx, resolved_layer.world_transform.position.z, node_ctx);
                }
                continue;
            }

            {
                GraphNodeId clip_node = graph.add_node(
                    std::make_unique<ClipTransitionNode>(
                        "clip_" + std::string(ct.layer_a) + "_" + layer_name,
                        ct.spec, ct.from, ct.duration));
                graph.connect(a_out, clip_node);
                graph.connect(b_out, clip_node);

                BuilderContext node_ctx{.layer_id = layer_name};
                const bool is_static = layer.cache_static || is_static_val;
                detail::append_composite_pass(
                    graph, ctx.current_output, clip_node, layer, is_static,
                    rctx, resolved_layer.world_transform.position.z, node_ctx);
            }
            continue;
        }

        // Layers referenced as clip-transition sources are rendered only
        // as sub-pipelines; skip their normal compositing pass.
        if (ctx.clip_transition_source_names.count(layer_name)) {
            flush_3d_bin();
            build_clip_sub_pipeline(resolved_layer);
            continue;
        }

        if (cam25d.enabled && layer.uses_2_5d_projection) {
            LayerGraphItem item = make_layer_graph_item(resolved_layer, cam25d, rctx, is_static_val);
            current_3d_bin.push_back(item);
        } else {
            flush_3d_bin();
            append_item(make_layer_graph_item(resolved_layer, cam25d, rctx, is_static_val));
        }
    }

    flush_3d_bin();
}

} // namespace chronon3d::graph::detail
