#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace chronon3d::renderer {

constexpr f32 kPathEpsilon = 1e-6f;

inline Vec2 transform_point(const Mat4& model, Vec2 p) {
    const Vec4 v = model * Vec4{p.x, p.y, 0.0f, 1.0f};
    if (std::abs(v.w) < kPathEpsilon) {
        return {v.x, v.y};
    }
    return {v.x / v.w, v.y / v.w};
}

inline bool is_prefetch_enabled() {
    static bool enabled = []() {
        const char* env = std::getenv("CHRONON_PREFETCH");
        if (env && std::string_view(env) == "0") {
            return false;
        }
        return true;
    }();
    return enabled;
}

inline void chrono_prefetch(const void* addr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr, 0, 3);
#elif defined(_MSC_VER)
    #if defined(_M_X64) || defined(_M_IX86)
    _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
    #endif
#endif
}

} // namespace chronon3d::renderer
