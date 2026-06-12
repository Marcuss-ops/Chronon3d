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

namespace chronon3d::graph::detail {

// Forward declaration (full definition in builder/graph_builder_internal.hpp)
struct LayerResolutionResult;

/// Returns true when a layer is compatible with dirty-rect partial updates.
/// Blur, motion blur, distort, temporal effects, non-Normal blend modes,
/// and active masks all bleed outside the geometric bounding box and
/// require a full-frame fallback.
[[nodiscard]] bool is_safe_for_dirty_rects(const Layer& layer, bool motion_blur_enabled);

/// Returns true when any active layer in the scene has spatial effects
/// (blur, glow, bloom, shadow, distort, temporal) that would cause visible
/// seams at tile boundaries during tile execution.
[[nodiscard]] bool has_layer_with_spatial_effects(
    const LayerResolutionResult& resolved,
    Frame frame
);

} // namespace chronon3d::graph::detail
