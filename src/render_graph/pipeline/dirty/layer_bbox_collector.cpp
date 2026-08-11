// ---------------------------------------------------------------------------
// dirty/layer_bbox_collector.cpp — Parallel layer bbox computation
// ---------------------------------------------------------------------------

#include "layer_bbox_collector.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include "../../builder/graph_builder_internal.hpp"
#include "../../builder/graph_builder_pipeline.hpp"
#include "../../builder/graph_builder_bbox.hpp"
#include "../../builder/evaluated_layer_placement.hpp"
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
        const LayerGraphItem item = resolve_layer_graph_item(rl, ctx);
        if (!item.visible) {
            return raster::BBox{0, 0, 0, 0};
        }
        return detail::compute_layer_bbox(item, ctx, sw_renderer);
    };

    (void)cam25d;
    tbb::enumerable_thread_specific<std::unordered_map<std::string, LayerBBoxState>> tls_bboxes;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, resolved.layers.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            auto& local_map = tls_bboxes.local();
            for (size_t i = r.begin(); i < r.end(); ++i) {
                const auto& rl = resolved.layers[i];
                if (rl.layer && rl.layer->active_at(frame)) {
                    raster::BBox bbox = compute_bbox_for_resolved(rl);

                    // A projected 2.5D layer can change its raster footprint
                    // without changing the source/layout bbox.  A partial
                    // dirty restore can therefore leave the projected card
                    // outside the redraw region and produce a blank frame.
                    // Keep the optimization for ordinary 2D layers, but use
                    // the conservative full-frame fallback for 2.5D.
                    if (rl.layer->uses_2_5d_projection ||
                        !is_safe_for_dirty_rects(*rl.layer,
                                                   chronon3d::is_motion_blur_active(settings.motion_blur),
                                                   ctx.services.effect_catalog)) {
                        bbox = raster::BBox{0, 0, width, height};
                        if (ctx.node_exec.counters) {
                            ctx.node_exec.counters->increment_dirty_full_fallback_reason(
                                DirtyFallbackReason::EffectBoundsUnknown);
                        }
                    } else {
                        const f32 spread = compute_layer_spatial_spread(*rl.layer);
                        if (spread > 0.0f) {
                            bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
                            bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
                            bbox.x1 = std::min(width,  static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
                            bbox.y1 = std::min(height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));
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
                        const SampleTime blur_time =
                            rl.layer->local_time(ctx.frame_input.sample_time);
                        content_h = hash_combine(
                            content_h,
                            hash_value(rl.layer->anim_transform.blur.evaluate(blur_time)));
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
