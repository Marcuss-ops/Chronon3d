#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run.hpp>
#include "text_run/text_run_execution.hpp"
#include "text_run/text_run_transform.hpp"
#endif
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    RenderNodeCachePolicy policy
) : RenderGraphNode(policy), m_name(std::move(name)), m_items(std::move(items)), m_key(key) {
    normalize_items(m_items);
}

std::optional<raster::BBox> MultiSourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    // CHRONON3D_PROJ_DIAG: indexed-for loop so item index reflects actual vector
    // position even when null items are skipped (range-for + manual counter was
    // bug-prone: counter incremented only in null-item-skip branch, leaving
    // bbox_i stale at 0 for non-null iterations where the diagnostic fires).
    for (std::size_t bbox_i = 0; bbox_i < m_items.size(); ++bbox_i) {
        const auto& item = m_items[bbox_i];
        if (!item.node) continue;
        const auto placement = detail::evaluate_source_payload_placement(
            item.matrix,
            item.opacity,
            ctx,
            item.apply_camera_projection,
            item.defer_camera_projection,
            item.native_3d,
            m_name,
            "predicted_bbox",
            bbox_i);
        if (!placement) {
            continue;
        }
        const Mat4 matrix = placement->render_matrix;

        f32 spread = 0.0f;
        spread += 8.0f;

        raster::BBox bbox;
#ifdef CHRONON3D_ENABLE_TEXT
        if (item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value, matrix, spread);
        } else
#endif
        // TICKET-122 FASE 3: GridPlane now uses 2.5D projection,
        // standard compute_world_bbox path (not native 3D).
        if (item.node->shape.type() == ShapeType::Mesh ||
            (ctx.frame_input.has_camera_2_5d &&
             item.node->shape.type() == ShapeType::FakeBox3D)) {
            if (auto proj_bbox = detail::projected_native_3d_bbox(
                    ctx, *item.node, matrix, spread)) {
                bbox = *proj_bbox;
            } else {
                bbox = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
            }
        } else {
            bbox = renderer::compute_world_bbox(item.node->shape, matrix, spread);
        }

        // Keep the diagnostic/world-space bounds separate from the execution
        // bounds. Diagnostics may inspect the unclipped item bbox, but the
        // aggregate consumed by culling, tile pruning, dirty clipping, and
        // cache state must always be canvas-clipped.
        const auto diagnostic_bbox = bbox;
        const auto execution = detail::resolve_execution_bbox(
            *placement, diagnostic_bbox, ctx);
        if (!execution) {
            continue;
        }
        const auto execution_bbox = *execution;

        if (ctx.policy.diagnostics_enabled) {
            spdlog::debug(
                "[multi-source-bbox] node='{}' item#{} diagnostic=[{},{},{},{}] execution=[{},{},{},{}]",
                m_name,
                bbox_i,
                diagnostic_bbox.x0, diagnostic_bbox.y0,
                diagnostic_bbox.x1, diagnostic_bbox.y1,
                execution_bbox.x0, execution_bbox.y0,
                execution_bbox.x1, execution_bbox.y1);
        }

        if (!execution_bbox.is_empty()) {
            x0 = std::min(x0, execution_bbox.x0);
            y0 = std::min(y0, execution_bbox.y0);
            x1 = std::max(x1, execution_bbox.x1);
            y1 = std::max(y1, execution_bbox.y1);
            has_any = true;
        }
    }

    if (!has_any) {
        return raster::BBox{0, 0, 0, 0};
    }
    return raster::BBox{x0, y0, x1, y1};
}

std::optional<raster::BBox> detail::preflight_diagnostic_bbox(
    const MultiSourceNode& node,
    const RenderGraphContext& ctx) {
    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    for (std::size_t item_index = 0; item_index < node.m_items.size(); ++item_index) {
        const auto& item = node.m_items[item_index];
        if (!item.node) continue;
        const auto placement = detail::evaluate_source_payload_placement(
            item.matrix, item.opacity, ctx, item.apply_camera_projection,
            item.defer_camera_projection, item.native_3d, node.m_name,
            "diagnostic_bbox", item_index);
        if (!placement) continue;

        raster::BBox bbox;
#ifdef CHRONON3D_ENABLE_TEXT
        if (item.node->shape.type() == ShapeType::TextRun &&
            item.node->shape.text_run_shape_handle().value) {
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value,
                placement->render_matrix, 8.0f);
        } else
#endif
        if (item.node->shape.type() == ShapeType::Mesh ||
            (ctx.frame_input.has_camera_2_5d &&
             item.node->shape.type() == ShapeType::FakeBox3D)) {
            const auto projected = detail::projected_native_3d_bbox(
                ctx, *item.node, placement->render_matrix, 8.0f);
            if (!projected) continue;
            bbox = *projected;
        } else {
            bbox = renderer::compute_world_bbox(
                item.node->shape, placement->render_matrix, 8.0f);
        }

        x0 = std::min(x0, bbox.x0);
        y0 = std::min(y0, bbox.y0);
        x1 = std::max(x1, bbox.x1);
        y1 = std::max(y1, bbox.y1);
        has_any = true;
    }

    if (!has_any) return raster::BBox{0, 0, 0, 0};
    return raster::BBox{x0, y0, x1, y1};
}

cache::NodeCacheKey MultiSourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // Fase A6 — frame discriminator for animator-driven text changes.
    // With glyphs no longer mutated in-place, the text_run hash fold
    // below only captures doc-driven changes (Scramble/Morph/Crossfade).
    // Animator-driven changes (position/opacity/scale/blur per-frame)
    // need the frame number to invalidate the cache.  Fold the integral
    // frame so consecutive frames with different animator states don't
    // share a stale entry.
    key.params_hash = hash_combine(key.params_hash, hash_value(static_cast<u64>(ctx.frame_input.sample_time.integral_frame())));
    key.frame = cache_frame_for_policy(cache_policy(), ctx.frame_input.frame);
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.policy.modular_coordinates));

    // Hash every item's full world matrix and opacity so the cache key
    // changes when the layer-level animation (e.g. tracking_breathing)
    // produces a different transform.
    for (const auto& item : m_items) {
        key.params_hash = hash_combine(key.params_hash, hash_value(item.matrix));
        key.params_hash = hash_combine(key.params_hash, hash_value(item.opacity));
        const auto groups = item.groups.empty() && item.group
            ? std::vector<MultiSourceItem::GroupIsolationSpec>{*item.group}
            : item.groups;
        for (const auto& group : groups) {
            key.params_hash = hash_combine(
                key.params_hash, hash_value(static_cast<std::uint64_t>(group.id)));
            key.params_hash = hash_combine(key.params_hash, hash_value(group.opacity));
            if (group.opacity_animation) {
                const auto& animation = *group.opacity_animation;
                key.params_hash = hash_combine(key.params_hash,
                    hash_value(static_cast<u64>(animation.start_frame)));
                key.params_hash = hash_combine(key.params_hash,
                    hash_value(static_cast<u64>(animation.end_frame)));
                key.params_hash = hash_combine(key.params_hash, hash_value(animation.from));
                key.params_hash = hash_combine(key.params_hash, hash_value(animation.to));
                key.params_hash = hash_combine(key.params_hash, hash_value(animation.discrete));
            }
        }
#ifdef CHRONON3D_ENABLE_TEXT
        // text_run items also fold the per-glyph animated state of
        // the underlying TextRunShape so the cache key invalidates when
        // `evaluate_animator_stack` mutates glyph state.  Without this
        // fold two animated frames with identical geometry would hit a
        // stale cache entry.  PR 10: use the frame overload so
        // Scramble / Morph / DissolveLayouts / font-swap Cut frames
        // driven by an AnimatedTextDocument also invalidate correctly.
        if (item.node && item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            key.params_hash = hash_combine(
                key.params_hash,
                chronon3d::hash_text_run_shape(
                    *item.node->shape.text_run_shape_handle().value,
                    ctx.frame_input.sample_time.integral_frame()));
        }
#endif
    }

    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B):
    // canonical conditional fold — single implementation for the invariant.
    cache::fold_camera_if(
        key, ctx.frame_input.has_camera_2_5d, ctx.frame_input.camera_2_5d);

    return key;
}



} // namespace chronon3d::graph
