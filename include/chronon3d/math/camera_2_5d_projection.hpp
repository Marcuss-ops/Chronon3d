#pragma once

#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/quat.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace chronon3d {

struct ProjectedLayer2_5D {
    Transform transform;
    Mat4      projection_matrix{1.0f}; // Matrix that maps world-space to projected screen-space
    f32       depth{1000.0f};
    f32       perspective_scale{1.0f};
    bool      visible{true}; // false when layer is behind or on the camera plane
};

// Returns the focal length (pixels) for a given vertical FOV and viewport height.
// focal = (viewport_height / 2) / tan(fov_rad / 2)
// At depth == focal_length, perspective_scale == 1.
inline f32 focal_length_from_fov(f32 viewport_height, f32 fov_deg) {
    const f32 fov_rad = fov_deg * (glm::pi<f32>() / 180.0f);
    return (viewport_height * 0.5f) / std::tan(fov_rad * 0.5f);
}

inline Mat4 get_camera_view_matrix(const Camera2_5D& camera) {
    Vec3 eye = camera.position;
    Vec3 target = camera.point_of_interest;
    
    // In our world, Y is Up. Screen space Y is down.
    // For lookAt, we use world Up (0, 1, 0).
    Vec3 up{0, 1, 0}; 
    
    // If POI and eye are same, POI is ignored.
    bool use_poi = (glm::length(target - eye) > 0.001f);
    
    Mat4 view;
    if (use_poi) {
        view = glm::lookAt(eye, target, up);
    } else {
        // Use Euler rotation.
        Mat4 trans = glm::translate(Mat4(1.0f), -eye);
        // Inverse rotation because we want world-to-camera.
        Mat4 rot   = glm::mat4_cast(glm::inverse(math::from_euler(camera.rotation)));
        view = rot * trans;
    }
    
    return view;
}

// Project a 3D layer transform through a Camera2_5D into screen space.
inline ProjectedLayer2_5D project_layer_2_5d(
    const Transform& layer_transform,
    const Camera2_5D& camera,
    f32 viewport_width,
    f32 viewport_height
) {
    ProjectedLayer2_5D out;
    out.transform = layer_transform;

    // View matrix transforms from world to camera space.
    Mat4 view = get_camera_view_matrix(camera);
    
    // Position of the layer in camera space.
    Vec4 world_pos{layer_transform.position.x, layer_transform.position.y, layer_transform.position.z, 1.0f};
    Vec4 cam_pos = view * world_pos;

    // In camera space (lookAt):
    // Eye is at origin. Target is along -Z.
    // So objects in front of camera have negative Z.
    // Depth for sorting should be positive.
    const f32 depth = -cam_pos.z;

    // Cull layers that are behind or touching the camera plane.
    if (depth <= 0.0f) {
        out.visible = false;
        return out;
    }

    const f32 focal = (camera.projection_mode == Camera2_5DProjectionMode::Fov)
        ? focal_length_from_fov(viewport_height, camera.fov_deg)
        : camera.zoom;

    const f32 perspective_scale = focal / depth;

    // Centroid projection for backward compatibility and depth sorting.
    out.transform.position.x = cam_pos.x * perspective_scale;
    out.transform.position.y = -cam_pos.y * perspective_scale; 
    out.transform.position.z = 0.0f;

    out.transform.scale.x *= perspective_scale;
    out.transform.scale.y *= perspective_scale;

    out.depth             = depth;
    out.perspective_scale = perspective_scale;

    // Calculate the full world-to-screen matrix for the TransformNode to use.
    // DstScene <- World
    Mat4 proj = Mat4(0.0f);
    proj[0][0] = focal;
    proj[1][1] = focal; 
    proj[2][2] = 1.0f;   // Keep Z
    proj[2][3] = -1.0f;  // Perspective w = -z
    proj[3][3] = 0.0001f; // Tiny offset to make it invertible
    
    // Final matrix: Proj * View * Model
    out.projection_matrix = proj * view * layer_transform.to_mat4();

    return out;
}

} // namespace chronon3d
