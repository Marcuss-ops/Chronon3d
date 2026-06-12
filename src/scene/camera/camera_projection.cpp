// ============================================================================
// camera_projection.cpp — Camera Projection Helpers
//
/// @file    camera_projection.cpp
/// @brief   Project world-space points and quads to screen space.
///
/// All core math now delegates to camera_projection_contract.hpp so that
/// every projection path in Chronon3D uses the same sign, centre, depth,
/// and FOV/zoom conventions.
// ============================================================================

#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
#include <cmath>
#include <algorithm>
#include <limits>

namespace chronon3d {

ScreenPoint project_world_to_screen(
    const Vec3& world,
    const Camera2_5D& camera,
    Viewport viewport
) {
    auto p = camera_math::project_world_point(
        camera,
        world,
        camera_math::Viewport2D{viewport.width, viewport.height}
    );

    ScreenPoint sp;
    sp.position = p.screen;
    sp.depth = p.depth;
    sp.behind_camera = !p.visible;
    return sp;
}

ProjectedBounds project_quad_to_screen(
    const std::array<Vec3, 4>& world_corners,
    const Camera2_5D& camera,
    Viewport viewport
) {
    ProjectedBounds pb;
    pb.min = Vec2{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    pb.max = Vec2{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

    int visible_count = 0;
    for (size_t i = 0; i < 4; ++i) {
        auto p = camera_math::project_world_point(
            camera,
            world_corners[i],
            camera_math::Viewport2D{viewport.width, viewport.height}
        );
        if (p.visible) {
            pb.min.x = std::min(pb.min.x, p.screen.x);
            pb.min.y = std::min(pb.min.y, p.screen.y);
            pb.max.x = std::max(pb.max.x, p.screen.x);
            pb.max.y = std::max(pb.max.y, p.screen.y);
            visible_count++;
        }
    }

    if (visible_count == 0) {
        pb.min = Vec2{0.0f, 0.0f};
        pb.max = Vec2{0.0f, 0.0f};
        pb.visible_ratio = 0.0f;
        pb.intersects_canvas = false;
        pb.fully_inside_canvas = false;
        pb.fully_inside_safe_area = false;
        return pb;
    }

    float bounds_area = (pb.max.x - pb.min.x) * (pb.max.y - pb.min.y);
    if (bounds_area <= 1e-4f) bounds_area = 1.0f;

    float cx1 = std::max(pb.min.x, 0.0f);
    float cy1 = std::max(pb.min.y, 0.0f);
    float cx2 = std::min(pb.max.x, viewport.width);
    float cy2 = std::min(pb.max.y, viewport.height);
    float overlap_canvas = (cx2 > cx1 && cy2 > cy1) ? (cx2 - cx1) * (cy2 - cy1) : 0.0f;

    pb.visible_ratio = overlap_canvas / bounds_area;
    pb.intersects_canvas = (overlap_canvas > 0.0f);
    pb.fully_inside_canvas = (pb.min.x >= 0.0f && pb.max.x <= viewport.width &&
                              pb.min.y >= 0.0f && pb.max.y <= viewport.height);

    float sx1 = viewport.width * 0.05f;
    float sy1 = viewport.height * 0.05f;
    float sx2 = viewport.width * 0.95f;
    float sy2 = viewport.height * 0.95f;

    pb.fully_inside_safe_area = (pb.min.x >= sx1 && pb.max.x <= sx2 &&
                                 pb.min.y >= sy1 && pb.max.y <= sy2);

    return pb;
}

ProjectedBounds project_layer_bounds_to_screen(
    const Transform3D& layer_world,
    Vec2 layer_size,
    const Camera2_5D& camera,
    Viewport viewport
) {
    Mat4 world_m = layer_world.to_mat4();
    std::array<Vec3, 4> corners = {
        Vec3(world_m * Vec4(0.0f, 0.0f, 0.0f, 1.0f)),
        Vec3(world_m * Vec4(layer_size.x, 0.0f, 0.0f, 1.0f)),
        Vec3(world_m * Vec4(layer_size.x, layer_size.y, 0.0f, 1.0f)),
        Vec3(world_m * Vec4(0.0f, layer_size.y, 0.0f, 1.0f))
    };
    return project_quad_to_screen(corners, camera, viewport);
}

} // namespace chronon3d
