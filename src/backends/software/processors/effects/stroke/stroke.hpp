#pragma once

// ---------------------------------------------------------------------------
// stroke.hpp — Declarations for scalar Stroke effect (A7)
// ---------------------------------------------------------------------------
//
// Implements morphological stroke using Chebyshev distance with a square
// separable kernel.
//
// Operations:
//   - Dilate(fb, r):   expands opaque regions by r pixels (max alpha in r×r)
//   - Erode(fb, r):    shrinks opaque regions by r pixels (min alpha in r×r)
//   - apply_stroke():  combines dilate/erode to produce an outline at the
//                      alpha boundary with optional softness.

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/color.hpp>
#include <cstdint>
#include <optional>

namespace chronon3d {

// Forward declaration — full definition in effect_params.hpp
enum class StrokeMode;

} // namespace chronon3d

namespace chronon3d::renderer {

/// Compute bounding-box expansion for a stroke.
/// \param width   Stroke width in pixels
/// \param softness Edge softness in pixels
/// \param mode    StrokeMode — affects the expansion formula
/// \return (marginX, marginY) — same value for both axes (square kernel).
[[nodiscard]] std::pair<int, int> stroke_margins(float width, float softness,
                                                  StrokeMode mode);

/// Apply a stroke effect to `fb`.
/// \param fb         Framebuffer to modify in-place
/// \param color      Stroke colour (straight RGBA)
/// \param width      Stroke width in pixels
/// \param softness   Edge softness in pixels (0 = hard)
/// \param mode       Outside | Inside | Center
/// \param clip       Optional clip rectangle
void apply_stroke(
    Framebuffer& fb,
    const Color& color,
    float width,
    float softness,
    StrokeMode mode,
    const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
