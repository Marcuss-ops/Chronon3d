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

/// Injected prefetch setting (set once at startup by SoftwareRenderer).
inline bool& prefetch_enabled_flag() {
    static bool enabled = true;
    return enabled;
}

inline void set_prefetch_enabled(bool enabled) {
    prefetch_enabled_flag() = enabled;
}

inline bool is_prefetch_enabled() {
    return prefetch_enabled_flag();
}

inline void chrono_prefetch(const void* addr) {
    chronon3d::prefetch(addr, false, 3);
}

} // namespace chronon3d::renderer
