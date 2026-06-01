#pragma once

#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include "../builder/graph_builder_internal.hpp"
#include <unordered_map>
#include <optional>

namespace chronon3d::graph::detail {

// ── Camera change detection ──────────────────────────────────────────────────
// Compares the current camera with the previously stored state.
[[nodiscard]] inline bool camera_changed(
    const Camera2_5D& current,
    const Camera2_5D* prev,
    bool prev_valid
) {
    if (!current.enabled) {
        if (!prev_valid || !prev->enabled) return false;
        return true;
    }
    if (!prev_valid) return true;
    if (!prev->enabled) return true;
    return prev->position      != current.position  ||
           prev->zoom          != current.zoom      ||
           prev->fov_deg       != current.fov_deg   ||
           prev->rotation      != current.rotation  ||
           prev->projection_mode != current.projection_mode ||
           prev->point_of_interest_enabled != current.point_of_interest_enabled ||
           prev->point_of_interest != current.point_of_interest ||
           prev->parent_name != current.parent_name ||
           prev->target_name != current.target_name ||
           prev->hierarchy_baked != current.hierarchy_baked;
}

// ── Dirty rect safety check ──────────────────────────────────────────────────
// Returns true when a layer's effects, blend mode, and mask are compatible
// with dirty-rect partial updates.  Blur, motion blur, distort, temporal
// effects, non-Normal blend modes, and active masks all bleed outside the
// geometric bounding box and require a full-frame fallback.
[[nodiscard]] inline bool is_safe_for_dirty_rects(const Layer& layer, bool motion_blur_enabled) {
    if (motion_blur_enabled) return false;
    if (layer.blend_mode != BlendMode::Normal) return false;
    if (layer.mask.enabled()) return false;
    
    const auto& registry = effects::EffectRegistry::instance();
    for (const auto& eff : layer.effects) {
        if (!eff.enabled) continue;
        
        effects::EffectCategory category = eff.descriptor.category;
        if (registry.contains(eff.descriptor.id)) {
            category = registry.get(eff.descriptor.id).category;
        }
        
        // Blur, distort, temporal, and light effects (glow/bloom/shadow)
        // expand the effective bbox beyond the geometric bounds — dirty
        // rects and tile execution cannot track them without causing seams
        // at tile boundaries.
        if (category == effects::EffectCategory::Blur)     return false;
        if (category == effects::EffectCategory::Distort)  return false;
        if (category == effects::EffectCategory::Temporal) return false;
        if (category == effects::EffectCategory::Light)    return false;
    }
    return true;
}

// ── Spatial effects detection (for tile execution safety) ────────────────────
// Checks whether any layer has spatial effects that would cause visible seams
// at tile boundaries during tile execution.  Unlike is_safe_for_dirty_rects,
// this is an unconditional scan of ALL active layers (not just dirty ones)
// because tile execution re-executes the full graph per tile, and even a
// static glow/bloom/shadow layer would have its effect clipped to the tile
// boundary — producing seams where the blur reads zero/garbage pixels from
// outside the tile.
[[nodiscard]] inline bool has_layer_with_spatial_effects(
    const detail::LayerResolutionResult& resolved,
    Frame frame
) {
    const auto& registry = effects::EffectRegistry::instance();
    for (const auto& rl : resolved.layers) {
        if (!rl.layer || !rl.layer->active_at(frame)) continue;
        for (const auto& eff : rl.layer->effects) {
            if (!eff.enabled) continue;
            effects::EffectCategory category = eff.descriptor.category;
            if (registry.contains(eff.descriptor.id)) {
                category = registry.get(eff.descriptor.id).category;
            }
            if (category == effects::EffectCategory::Blur)     return true;
            if (category == effects::EffectCategory::Distort)  return true;
            if (category == effects::EffectCategory::Temporal) return true;
            if (category == effects::EffectCategory::Light)    return true;
        }
    }
    return false;
}

// ── Dirty rect computation ───────────────────────────────────────────────────
// Computes per-layer bounding boxes (TBB parallel), diffs them against the
// previous frame's bboxes, and optionally applies the scroll optimisation.
//
// Returns the union dirty rectangle (nullopt = full frame).
struct DirtyRectOutput {
    std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState> layer_bboxes;
    std::optional<raster::BBox> dirty_rect;
    bool use_dirty_rects{false};

    // Tile-based dirty tracking (V2): populated when settings.tile_size > 0
    // and settings.enable_dirty_bitmask are both active.
    // Falls back to dirty_rect when tile-based tracking is not available.
    std::optional<raster::TileGrid> tile_grid;
    std::optional<raster::DirtyTileMask> dirty_tiles;
    bool use_dirty_tiles{false};
};

DirtyRectOutput compute_dirty_rect(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Scene& scene,
    const RenderSettings& settings,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    i32 width,
    i32 height
);

} // namespace chronon3d::graph::detail
