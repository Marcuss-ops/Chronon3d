#pragma once

// ── Dirty-rect safety policies ─────────────────────────────────────────────
//
/// @file    dirty_safety_policy.hpp
/// @brief   Policies that determine whether a layer or scene is compatible
///          with dirty-rect partial updates and tile execution.
///
/// Extracted from scene_internal.hpp so these policies can be unit-tested and
/// evolved independently.

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/core/types/frame.hpp>

namespace chronon3d::effects {
    class EffectCatalog;
}

namespace chronon3d::graph::detail {

// Forward declaration (full definition in builder/graph_builder_internal.hpp)
struct LayerResolutionResult;

/// Returns true when a layer is compatible with dirty-rect partial updates.
/// Motion blur, distort + temporal effects, non-Normal blend modes,
/// and active masks all bleed outside the geometric bounding box and
/// require a full-frame fallback.
///
/// Blur and Light effects (glow / bloom / shadow) are now ALLOWED here:
/// their spatial extent is predictable, so the caller dilates the layer's
/// geometric bbox via `compute_layer_spatial_spread()` instead of falling
/// back to full-frame.
[[nodiscard]] bool is_safe_for_dirty_rects(const Layer& layer, bool motion_blur_enabled,
                                              const effects::EffectCatalog* catalog);

/// Returns the maximum spatial spread (in pixels) of any Blur or Light
/// effect in the layer's effect stack.  Used to dilate dirty-rect
/// bounding boxes so that Light/Blur layers can be tracked with
/// partial-dirty rects instead of a full-frame fallback.
///
/// Adds a 2px safety margin for anti-aliasing fringes on top of the
/// raw radius-driven extent.  Returns 0.0f when the layer has no
/// spatial-expanding effects.
[[nodiscard]] f32 compute_layer_spatial_spread(const Layer& layer);

/// Returns true when any active layer in the scene has spatial effects
/// (blur, glow, bloom, shadow, distort, temporal) that would cause visible
/// seams at tile boundaries during tile execution.
[[nodiscard]] bool has_layer_with_spatial_effects(
    const LayerResolutionResult& resolved,
    Frame frame,
    const effects::EffectCatalog* catalog
);

} // namespace chronon3d::graph::detail
