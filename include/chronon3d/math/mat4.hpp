#pragma once

#include <chronon3d/core/types.hpp>
#include <array>

namespace chronon3d {

struct Mat4 {
    std::array<f32, 16> m{0.0f};

    constexpr Mat4() {
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
    }

    static constexpr Mat4 identity() { return Mat4(); }

    // Placeholder for full matrix implementation
    // We will expand this as needed for rendering
};

} // namespace chronon3d
