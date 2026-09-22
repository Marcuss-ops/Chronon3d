#pragma once

#include <chronon3d/math/glm_types.hpp>

#include <array>

namespace chronon3d::graph::detail {

struct HomogeneousQuad {
    std::array<Vec4, 8> vertices{};
    int count{0};
};

// Clip the source quad before perspective division. A projected corner with
// w≈0 is not a usable pixel coordinate; clipping preserves a bounded quad.
inline HomogeneousQuad clip_quad_against_w(const std::array<Vec4, 4>& input) {
    constexpr f32 kMinW = 1.0e-3f;
    HomogeneousQuad out;
    std::array<Vec4, 8> current{};
    int current_count = 4;
    for (int i = 0; i < current_count; ++i) current[i] = input[i];

    for (int i = 0; i < current_count; ++i) {
        const Vec4& a = current[i];
        const Vec4& b = current[(i + 1) % current_count];
        const bool a_inside = a.w >= kMinW;
        const bool b_inside = b.w >= kMinW;
        if (a_inside != b_inside) {
            const f32 t = (kMinW - a.w) / (b.w - a.w);
            out.vertices[out.count++] = a + t * (b - a);
        }
        if (b_inside) out.vertices[out.count++] = b;
    }
    return out;
}

} // namespace chronon3d::graph::detail

