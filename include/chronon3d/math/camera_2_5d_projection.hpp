#pragma once

#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/math/transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace chronon3d {

struct ProjectedLayer2_5D {
    Transform transform;
    f32 depth{1000.0f};
    f32 perspective_scale{1.0f};
    bool visible{true}; // false when layer is behind or on the camera plane
};

// Returns the focal length (pixels) for a given vertical FOV and viewport height.
// focal = (viewport_height / 2) / tan(fov_rad / 2)
// At depth == focal_length, perspective_scale == 1.
inline f32 focal_length_from_fov(f32 viewport_height, f32 fov_deg) {
    const f32 fov_rad = fov_deg * (glm::pi<f32>() / 180.0f);
    return (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
}

// Project a 3D layer transform through a Camera2_5D into screen space.
//
// Convention:
//   Z negative  = closer to camera (appears larger)
//   Z positive  = farther from camera (appears smaller)
//   Camera default at z=-1000, normal plane at z=0 → depth=1000, scale=1
//
// depth = layer.z - camera.z
// focal = zoom  (ProjectionMode::Zoom)   OR   focal_length_from_fov() (ProjectionMode::Fov)
// perspective_scale = focal / depth
// Camera X/Y movement creates parallax: nearer layers shift more.
inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Camera2_5D& camera,
    f32 viewport_width,
    f32 viewport_height
) {
    ProjectedLayer2_5D out;
    out.transform = layer_transform;

    const f32 depth = layer_transform.position.z - camera.position.z;

    // Cull layers that are behind or touching the camera plane.
    if (depth <= 0.0f) {
        out.visible = false;
        return out;
    }

    // Focal length depends on the projection mode.
    const f32 focal = (camera.projection_mode == Camera2_5DProjectionMode::Fov)
        ? focal_length_from_fov(viewport_height, camera.fov_deg)
        : camera.zoom;

    const f32 perspective_scale = focal / depth;

    const f32 cx = viewport_width  * 0.5f;
    const f32 cy = viewport_height * 0.5f;

    // Camera movement creates parallax: shift relative to viewport center.
    const f32 relative_x = layer_transform.position.x - camera.position.x;
    const f32 relative_y = layer_transform.position.y - camera.position.y;

    out.transform.position.x = relative_x * perspective_scale;
    out.transform.position.y = relative_y * perspective_scale;
    out.transform.position.z = 0.0f;

    out.transform.scale.x *= perspective_scale;
    out.transform.scale.y *= perspective_scale;

    out.depth             = depth;
    out.perspective_scale = perspective_scale;

    return out;
}

} // namespace chronon3d
