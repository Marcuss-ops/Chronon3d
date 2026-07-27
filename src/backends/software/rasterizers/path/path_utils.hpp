#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/memory_utils.hpp>
#include <cmath>

namespace chronon3d::renderer {

constexpr f32 kPathEpsilon = 1e-6f;

inline Vec2 transform_point(const Mat4& model, Vec2 p) {
    const Vec4 v = model * Vec4{p.x, p.y, 0.0f, 1.0f};
    if (std::abs(v.w) < kPathEpsilon) {
        return {v.x, v.y};
    }
    return {v.x / v.w, v.y / v.w};
}

// Prefetch is unconditional for the software path.  The old mutable
// process-wide flag had no active production setter and made rasterisation
// depend on hidden global state.
inline bool is_prefetch_enabled() { return true; }

inline void chrono_prefetch(const void* addr) {
    chronon3d::prefetch(addr, false, 3);
}

} // namespace chronon3d::renderer
