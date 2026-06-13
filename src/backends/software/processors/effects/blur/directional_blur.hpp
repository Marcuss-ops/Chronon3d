#pragma once

// ---------------------------------------------------------------------------
// directional_blur.hpp — Declarations for scalar Directional Blur
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/color.hpp>
#include <optional>

namespace chronon3d::renderer {

/// Compute the bounding-box expansion needed for a directional blur.
/// Returns (marginX, marginY) such that the affected region extends
/// marginX pixels left/right and marginY pixels top/bottom beyond the
/// original bounds.
[[nodiscard]] std::pair<int, int> directional_blur_margins(
    float angle_degrees, float length);

/// Apply a directional blur to `fb` along the given `angle` over `length`
/// pixels, using `samples` taps (0 = auto).
void apply_directional_blur(
    Framebuffer& fb,
    float angle_degrees,
    float length,
    int samples = 0,
    const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
