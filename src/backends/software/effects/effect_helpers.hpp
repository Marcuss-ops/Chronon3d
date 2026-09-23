#pragma once

// ---------------------------------------------------------------------------
// effect_helpers.hpp
//
// Internal utility functions shared across effect implementation files.
// Header-only (static inline).
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace chronon3d::renderer {

[[nodiscard]] static inline std::optional<raster::BBox> expand_effect_clip(
    const std::optional<raster::BBox>& clip,
    int width,
    int height,
    float spread
) {
    if (!clip) {
        return std::nullopt;
    }

    const int margin = static_cast<int>(std::ceil(std::max(0.0f, spread)));

    raster::BBox out = *clip;
    out.x0 -= margin;
    out.y0 -= margin;
    out.x1 += margin;
    out.y1 += margin;
    out.clip_to(width, height);
    return out;
}

} // namespace chronon3d::renderer
