#pragma once

// ---------------------------------------------------------------------------
// radial_blur.hpp — Declarations for scalar Radial Blur (A6)
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/color.hpp>
#include <optional>

namespace chronon3d::renderer {

/// Apply a radial blur (zoom + spin) to `fb`.
///
/// \param fb           Framebuffer to blur in-place
/// \param center_x     Normalized center X [0, 1]
/// \param center_y     Normalized center Y [0, 1]
/// \param amount       Blur strength; 0 = identity
/// \param samples      Number of taps (>= 2)
/// \param clip         Optional clip rectangle
void apply_radial_blur(
    Framebuffer& fb,
    float center_x, float center_y,
    float amount,
    int samples,
    const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
