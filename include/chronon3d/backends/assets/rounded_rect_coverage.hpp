#pragma once

#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace detail {

/// Anti-aliased coverage for a rounded rectangle.
///
/// The distance is measured to the inner rectangle [r, w-r] x [r, h-r].
/// Pixels on a straight edge therefore have distance zero and full coverage;
/// falloff is confined to the four corner arcs. The previous image-rounding
/// implementation faded the entire left/right radius band, including straight
/// edge rows, which produced a wide horizontal alpha gradient.
inline float rounded_rect_coverage(float x, float y, float w, float h,
                                   float radius) {
    if (radius <= 0.0f) {
        return 1.0f;
    }
    if (w <= 0.0f || h <= 0.0f) {
        return 0.0f;
    }

    const float r = std::min({radius, w * 0.5f, h * 0.5f});
    const float cx = std::clamp(x, r, w - r);
    const float cy = std::clamp(y, r, h - r);
    const float dx = x - cx;
    const float dy = y - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    return std::clamp(r + 0.5f - dist, 0.0f, 1.0f);
}

} // namespace detail
} // namespace chronon3d
