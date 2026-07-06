#include "graph_builder_passes.hpp"
#include "../graph_builder_pipeline.hpp"
#include "../graph_builder_coordinates.hpp"
#include "graph_builder_lighting_passes.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <span>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void LayerPipelinePass::run(GraphBuildContext& ctx) {
    auto& graph = *ctx.graph;
    auto& rctx  = *ctx.render_ctx;
    const auto& cam25d = ctx.cam25d;

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

    // ── Main layer iteration ───────────────────────────────────────────
    for (const auto& resolved_layer : ctx.resolved.layers) {
        const Layer& layer = *resolved_layer.layer;
        const bool is_static_val =
            ctx.is_static_cache.at(std::string(layer.name));

        if (!layer.active_at(rctx.frame_input.frame)) continue;

        // Matte source layers are consumed exclusively by TrackMatteNode.
        if (ctx.matte_source_names.count(std::string(layer.name))) {
            flush_3d_bin();
            continue;
        }

        if (layer.kind == LayerKind::Null) {
            flush_3d_bin();
            continue;
        }

        if (cam25d.enabled && layer.uses_2_5d_projection) {
            Transform effective_transform = resolved_layer.world_transform;
            const Mat4 projection_world_matrix = effective_transform.to_mat4();
            auto proj = project_layer_2_5d(
                effective_transform, projection_world_matrix, cam25d,
                static_cast<f32>(rctx.frame_input.width), static_cast<f32>(rctx.frame_input.height),
                rctx.policy.diagnostics_enabled);
            if (proj.visible) {
                const Mat4 eff_proj = is_native_3d_layer(layer)
                    ? Mat4(1.0f) : glm::translate(Mat4(1.0f), Vec3(proj.transform.position.x, proj.transform.position.y, 0.0f)) * glm::scale(Mat4(1.0f), Vec3(proj.perspective_scale, proj.perspective_scale, 1.0f));
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
                .is_static       = is_static_val,
            });
        }
    }

    flush_3d_bin();
}

} // namespace chronon3d::graph::detail
