#pragma once

#include <chronon3d/math/color.hpp>
#include <tracy/Tracy.hpp>

namespace chronon3d {
namespace compositor {

/**
 * Standard alpha blending (Normal mode)
 * out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
 * out.a   = src.a + dst.a * (1 - src.a)
 * 
 * This assumes non-premultiplied alpha.
 */
inline Color blend_normal(const Color& src, const Color& dst) {
    ZoneScoped;
    f32 out_a = src.a + dst.a * (1.0f - src.a);
    
    // If output alpha is zero, color doesn't matter much but we avoid NaN
    if (out_a <= 0.0f) return {0, 0, 0, 0};

    // User requested formula: out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
    // Note: To be fully correct for destination alpha < 1, the dst part should be scaled by dst.a
    // But we follow the requested simple/common formula.
    f32 r = src.r * src.a + dst.r * (1.0f - src.a);
    f32 g = src.g * src.a + dst.g * (1.0f - src.a);
    f32 b = src.b * src.a + dst.b * (1.0f - src.a);

    return {r, g, b, out_a};
}

} // namespace compositor
} // namespace chronon3d
