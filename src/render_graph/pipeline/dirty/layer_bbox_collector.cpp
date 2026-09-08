// ---------------------------------------------------------------------------
// dirty/layer_bbox_collector.cpp — Parallel layer bbox computation
// ---------------------------------------------------------------------------

#include "layer_bbox_collector.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
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

#include <utility>
#include <vector>

namespace chronon3d::graph::detail {

void populate_node_semantic_fingerprints(
    const RenderNode& node,
    LayerBBoxState& state) {
    using namespace chronon3d::graph;

    state.semantic_presence |= SemanticColor;
    state.color_hash = hash_combine(state.color_hash, hash_color(node.color));
    state.color_hash = hash_combine(state.color_hash, hash_fill(node.fill));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_string(node.name));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_value(static_cast<int>(node.shape.type())));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_surface_policy(node.surface_policy));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_transform_policy(node.transform_policy));

    switch (node.shape.type()) {
        case ShapeType::TextRun:
            state.semantic_presence |= SemanticText;
            state.text_hash = hash_combine(state.text_hash, hash_shape(node.shape));
            break;
        case ShapeType::Image:
        case ShapeType::TiledImage:
            state.semantic_presence |= SemanticImage;
            state.image_hash = hash_combine(state.image_hash, hash_shape(node.shape));
            break;
        default:
            break;
    }
}

void populate_layer_semantic_fingerprints(
    const Layer& layer,
    SampleTime sample_time,
    LayerBBoxState& state) {
    using namespace chronon3d::graph;

    state.structure_hash = hash_combine(state.structure_hash, hash_string(layer.name));
    state.structure_hash = hash_combine(state.structure_hash, hash_string(layer.parent_name));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_value(static_cast<int>(layer.kind)));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_value(layer.nodes.size()));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_value(layer.uses_2_5d_projection));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_value(layer.screen_space));
    state.structure_hash = hash_combine(
        state.structure_hash, hash_value(static_cast<int>(layer.blend_mode)));
    for (const auto& node : layer.nodes) {
        populate_node_semantic_fingerprints(node, state);
    }

    if (layer.m_effects && !layer.m_effects->empty()) {
        state.semantic_presence |= SemanticEffects;
        state.effects_hash = hash_effect_stack(*layer.m_effects);
    }
    if (layer.anim_transform.blur.is_time_dependent()) {
        state.semantic_presence |= SemanticEffects;
        state.effects_hash = hash_combine(
            state.effects_hash,
            hash_value(layer.anim_transform.blur.evaluate(
                layer.local_time(sample_time))));
    }
    if (layer.kind == LayerKind::Video && layer.video_source) {
        state.semantic_presence |= SemanticVideoSource;
        state.video_source_hash = hash_video_source(*layer.video_source);
    }
    state.semantic_fingerprints_valid = true;
}

// Data-oriented per-worker accumulation: workers emit dense (index, state)
// entries instead of per-thread unordered_map<string, LayerBBoxState> nodes.
// The layer-name key is materialized exactly once per layer when the final
// name-keyed map (the caller contract) is built — no per-thread hash/node
// churn and no second merged map on the way out.
struct PendingLayerBBox {
    std::size_t resolved_index;
    LayerBBoxState state;
};

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
    // The processor registry is immutable for the whole frame: capture one
    // snapshot shared by every active layer instead of rebuilding the
    // (sorted, shared-owned) snapshot per visible node / per layer.
    const auto registry_snapshot = sw_renderer
        ? sw_renderer->software_registry().snapshot()
        : nullptr;

    auto compute_bbox_for_resolved = [&](const ResolvedLayer& rl) -> raster::BBox {
        const LayerGraphItem item = resolve_layer_graph_item(rl, ctx);
        if (!item.visible) {
            return raster::BBox{0, 0, 0, 0};
        }
        // compute_layer_bboxes_parallel is only ever reached with a live
        // software renderer (compute_dirty_rect early-outs otherwise), so the
        // frame snapshot above is normally available here.  Defensive
        // fallback mirrors the legacy renderer-less full-frame behavior.
        if (!registry_snapshot) {
            return detail::compute_layer_bbox(item, ctx, sw_renderer);
        }
        return detail::compute_layer_bbox(item, ctx, *registry_snapshot);
    };

    (void)cam25d;
    tbb::enumerable_thread_specific<std::vector<PendingLayerBBox>> tls_entries;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, resolved.layers.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            auto& pending = tls_entries.local();
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
                        rl.layer->is_native_3d() ||
                        !is_safe_for_dirty_rects(*rl.layer,
                                                   chronon3d::is_motion_blur_active(settings.motion_blur),
                                                   ctx.effect_catalog)) {
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

                    PendingLayerBBox entry;
                    entry.resolved_index = i;
                    LayerBBoxState& state = entry.state;
                    state.bbox = bbox;
                    state.world_matrix = rl.world_matrix;
                    state.opacity = rl.world_transform.opacity;
                    state.visible = rl.layer->visible;
                    state.cache_static = rl.layer->cache_static;
                    state.uses_2_5d_projection =
                        rl.layer->uses_2_5d_projection || rl.layer->is_native_3d();
                    uint64_t content_h = rl.layer->get_static_hash();
                    if (rl.layer->anim_transform.blur.is_time_dependent()) {
                        const SampleTime blur_time =
                            rl.layer->local_time(ctx.frame_input.sample_time);
                        content_h = hash_combine(
                            content_h,
                            hash_value(rl.layer->anim_transform.blur.evaluate(blur_time)));
                    }
                    state.content_hash = content_h;
                    populate_layer_semantic_fingerprints(
                        *rl.layer, ctx.frame_input.sample_time, state);
                    pending.push_back(std::move(entry));
                }
            }
        }
    );

    // Single serial build of the name-keyed result map (the caller contract).
    // Per-worker chunk ranges are disjoint and iterated in resolved-index
    // order, so the final map content no longer depends on TBB worker
    // scheduling for the normal case (one active entry per layer name).
    std::unordered_map<std::string, LayerBBoxState> merged;
    merged.reserve(resolved.layers.size());
    for (auto& entries : tls_entries) {
        for (auto& entry : entries) {
            const auto& rl = resolved.layers[entry.resolved_index];
            // Layer::name is PMR-backed; the result map owns std::string
            // keys, so materialize the key exactly once per active layer
            // (previously once per layer per worker plus a second merged
            // map with its own keys on the way out).
            merged[std::string(rl.layer->name)] = std::move(entry.state);
        }
    }
    return merged;
}

} // namespace chronon3d::graph::detail
