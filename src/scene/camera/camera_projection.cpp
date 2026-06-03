#include <chronon3d/scene/camera/camera_projection.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d {

ScreenPoint project_world_to_screen(
    const Vec3& world,
    const Camera2_5D& camera,
    Viewport viewport
) {
    const Mat4 view = camera.view_matrix();
    const Vec4 cam_space = view * Vec4(world, 1.0f);
    ScreenPoint sp;
    sp.depth = cam_space.z;
    if (cam_space.z <= 1e-4f) {
        sp.behind_camera = true;
        sp.position = Vec2{-999999.0f, -999999.0f};
        return sp;
    }
    sp.behind_camera = false;
    const float focal = (camera.projection_mode == Camera2_5DProjectionMode::Fov)
        ? (viewport.height * 0.5f) / std::tan(glm::radians(camera.fov_deg) * 0.5f)
        : camera.zoom;
    const float ps = focal / cam_space.z;
    sp.position = Vec2{
        cam_space.x * ps + viewport.width * 0.5f,
        -cam_space.y * ps + viewport.height * 0.5f
    };
    return sp;
}

ProjectedBounds project_quad_to_screen(
    const std::array<Vec3, 4>& world_corners,
    const Camera2_5D& camera,
    Viewport viewport
) {
    ProjectedBounds pb;
    pb.min = Vec2{999999.0f, 999999.0f};
    pb.max = Vec2{-999999.0f, -999999.0f};

    int visible_count = 0;
    for (size_t i = 0; i < 4; ++i) {
        ScreenPoint sp = project_world_to_screen(world_corners[i], camera, viewport);
        if (!sp.behind_camera) {
            pb.min.x = std::min(pb.min.x, sp.position.x);
            pb.min.y = std::min(pb.min.y, sp.position.y);
            pb.max.x = std::max(pb.max.x, sp.position.x);
            pb.max.y = std::max(pb.max.y, sp.position.y);
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
