#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/shape/transform_3d.hpp>
#include <array>

namespace chronon3d {

struct Viewport {
    float width{1920.0f};
    float height{1080.0f};
};

struct ScreenPoint {
    Vec2 position{0.0f, 0.0f};
    float depth{0.0f};
    bool behind_camera{false};
};

struct ProjectedBounds {
    Vec2 min{0.0f, 0.0f};
    Vec2 max{0.0f, 0.0f};
    float visible_ratio{1.0f};
    bool intersects_canvas{true};
    bool fully_inside_canvas{true};
    bool fully_inside_safe_area{true};
};

ScreenPoint project_world_to_screen(
    const Vec3& world,
    const CameraProjectionSource& camera,
    Viewport viewport
);

ProjectedBounds project_quad_to_screen(
    const std::array<Vec3, 4>& world_corners,
    const CameraProjectionSource& camera,
    Viewport viewport
);

ProjectedBounds project_layer_bounds_to_screen(
    const Transform3D& layer_world,
    Vec2 layer_size,
    const CameraProjectionSource& camera,
    Viewport viewport
);

} // namespace chronon3d
