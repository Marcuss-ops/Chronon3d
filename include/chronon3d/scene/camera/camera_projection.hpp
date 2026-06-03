#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <array>

namespace chronon3d {

struct ProjectedBounds {
    Vec2 min{0.0f, 0.0f};
    Vec2 max{0.0f, 0.0f};
    bool fully_clipped{false};
};

Vec2 project_world_to_screen(
    const Vec3& world,
    const Camera2_5D& camera,
    Vec2 viewport
);

ProjectedBounds project_bounds_to_screen(
    const std::array<Vec3, 4>& corners,
    const Camera2_5D& camera,
    Vec2 viewport
);

} // namespace chronon3d
