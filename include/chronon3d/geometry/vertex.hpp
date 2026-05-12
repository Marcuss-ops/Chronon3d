#pragma once

#include <chronon3d/math/vec3.hpp>

namespace chronon3d {

struct Vertex {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 0.0f, 0.0f};
    Vec2 uv{0.0f, 0.0f};

    constexpr Vertex() = default;
    constexpr Vertex(Vec3 pos, Vec3 norm = {0.0f, 0.0f, 0.0f}, Vec2 tex = {0.0f, 0.0f})
        : position(pos), normal(norm), uv(tex) {}
};

} // namespace chronon3d
