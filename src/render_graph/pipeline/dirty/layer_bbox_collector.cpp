// ---------------------------------------------------------------------------
// dirty/layer_bbox_collector.cpp — Parallel layer bbox computation
// ---------------------------------------------------------------------------

#include "layer_bbox_collector.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "../../builder/graph_builder_internal.hpp"
#include "../../builder/graph_builder_pipeline.hpp"
#include "../../builder/graph_builder_bbox.hpp"
#include "../dirty_safety_policy.hpp"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>

namespace chronon3d::graph::detail {

std::unordered_map<std::string, LayerBBoxState> compute_layer_bboxes_parallel(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Camera2_5DRuntime& cam25d,
    SoftwareRenderer* sw_renderer,
    const RenderSettings& settings,
    i32 width,
    i32 height,
    Frame frame
) {
    auto compute_bbox_for_resolved = [&](const ResolvedLayer& rl) -> raster::BBox {
        LayerGraphItem item;
        if (cam25d.enabled && rl.layer->uses_2_5d_projection) {
            Transform effective_transform = rl.world_transform;
            auto proj = project_layer_2_5d(
                effective_transform, effective_transform.to_mat4(), cam25d,
                static_cast<f32>(width), static_cast<f32>(height), ctx.options.diagnostics_enabled);
            if (proj.visible) {
                const Mat4 eff_proj = detail::is_native_3d_layer(*rl.layer) ? Mat4(1.0f) : proj.projection_matrix;
                item = LayerGraphItem{
                    .layer = rl.layer,
                    .transform = proj.transform,
                    .world_matrix = rl.world_matrix,
                    .projection_matrix = eff_proj,
                    .depth = proj.depth,
                    .world_z = rl.world_transform.position.z,
                    .projected = true,
                    .native_3d = detail::is_native_3d_layer(*rl.layer),
                    .insertion_index = rl.insertion_index
                };
            } else {
                return raster::BBox{0, 0, 0, 0};
            }
        } else {
            item = LayerGraphItem{
                .layer = rl.layer,
                .transform = rl.world_transform,
                .world_matrix = rl.world_matrix,
                .depth = 0.0f,
                .world_z = rl.world_transform.position.z,
                .projected = false,
                .native_3d = detail::is_native_3d_layer(*rl.layer),
                .insertion_index = rl.insertion_index
            };
        }
        return detail::compute_layer_bbox(item, ctx, sw_renderer);
    };

    tbb::enumerable_thread_specific<std::unordered_map<std::string, LayerBBoxState>> tls_bboxes;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, resolved.layers.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            auto& local_map = tls_bboxes.local();
            for (size_t i = r.begin(); i < r.end(); ++i) {
                const auto& rl = resolved.layers[i];
                if (rl.layer && rl.layer->active_at(frame)) {
                    raster::BBox bbox = compute_bbox_for_resolved(rl);

                    if (!is_safe_for_dirty_rects(*rl.layer, settings.motion_blur.enabled)) {
                        bbox = raster::BBox{0, 0, width, height};
                        if (ctx.telemetry.counters) {
                            ctx.telemetry.counters->increment_dirty_full_fallback_reason(
                                DirtyFallbackReason::EffectBoundsUnknown);
                        }
                    }

                    LayerBBoxState state;
                    state.bbox = bbox;
                    state.world_matrix = rl.world_matrix;
                    state.opacity = rl.world_transform.opacity;
                    state.visible = rl.layer->visible;
                    state.cache_static = rl.layer->cache_static;
                    state.uses_2_5d_projection = rl.layer->uses_2_5d_projection;
                    uint64_t content_h = rl.layer->get_static_hash();
                    if (rl.layer->anim_transform.blur.is_time_dependent()) {
                        content_h = hash_combine(content_h, hash_value(rl.layer->anim_transform.blur.evaluate(frame)));
                    }
                    state.content_hash = content_h;
                    local_map[std::string(rl.layer->name)] = state;
                }
            }
        }
    );

    std::unordered_map<std::string, LayerBBoxState> merged;
    for (auto& local_map : tls_bboxes) {
        for (auto&& [name, state] : local_map) {
            merged[name] = std::move(state);
        }
    }
    return merged;
}

} // namespace chronon3d::graph::detail
