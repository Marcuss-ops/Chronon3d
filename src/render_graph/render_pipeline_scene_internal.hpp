#pragma once

#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include "builder/graph_builder_internal.hpp"
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
           prev->projection_mode != current.projection_mode;
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
    for (const auto& eff : layer.effects) {
        if (!eff.enabled) continue;
        // Blur, distort, and temporal effects expand the effective bbox
        // beyond the geometric bounds — dirty rects cannot track them.
        if (eff.descriptor.category == effects::EffectCategory::Blur)     return false;
        if (eff.descriptor.category == effects::EffectCategory::Distort)  return false;
        if (eff.descriptor.category == effects::EffectCategory::Temporal)  return false;
    }
    return true;
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
