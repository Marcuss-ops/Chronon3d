#include <chronon3d/scene/camera/camera_projection.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d {

Vec2 project_world_to_screen(
    const Vec3& world,
    const Camera2_5D& camera,
    Vec2 viewport
) {
    const Mat4 view = camera.view_matrix();
    const Vec4 cam_space = view * Vec4(world, 1.0f);
    if (cam_space.z <= 1e-4f) {
        return Vec2{-999999.0f, -999999.0f};
    }
    const f32 focal = (camera.projection_mode == Camera2_5DProjectionMode::Fov)
        ? (viewport.y * 0.5f) / std::tan(glm::radians(camera.fov_deg) * 0.5f)
        : camera.zoom;

    const f32 ps = focal / cam_space.z;
    return Vec2{
        cam_space.x * ps + viewport.x * 0.5f,
        -cam_space.y * ps + viewport.y * 0.5f
    };
}

ProjectedBounds project_bounds_to_screen(
    const std::array<Vec3, 4>& corners,
    const Camera2_5D& camera,
    Vec2 viewport
) {
    ProjectedBounds bounds;
    bounds.min = Vec2{999999.0f, 999999.0f};
    bounds.max = Vec2{-999999.0f, -999999.0f};
    int visible_count = 0;

    for (const auto& corner : corners) {
        Vec2 p = project_world_to_screen(corner, camera, viewport);
        if (p.x > -999998.0f) {
            bounds.min.x = std::min(bounds.min.x, p.x);
            bounds.min.y = std::min(bounds.min.y, p.y);
            bounds.max.x = std::max(bounds.max.x, p.x);
            bounds.max.y = std::max(bounds.max.y, p.y);
            visible_count++;
        }
    }

    if (visible_count == 0) {
        bounds.fully_clipped = true;
        bounds.min = Vec2{0.0f, 0.0f};
        bounds.max = Vec2{0.0f, 0.0f};
    } else {
        bounds.fully_clipped = false;
    }

    return bounds;
}

} // namespace chronon3d
