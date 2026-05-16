#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/quat.hpp>
#include <string>
#include <memory_resource>

namespace chronon3d {

struct MotionBlurSettings {
    bool enabled{false};
    int  samples{8};           // number of subframes to accumulate
    f32  shutter_angle{180.0f}; // degrees; 180 = half-frame exposure
};

struct DepthOfFieldSettings {
    bool  enabled{false};
    f32   focus_z{0.0f};      // world-space Z at which blur = 0
    f32   aperture{0.015f};   // blur per unit of Z distance from focus
    f32   max_blur{24.0f};    // clamp: pixels of blur at extreme depths
};

// How perspective strength is specified for Camera2_5D.
// Zoom: use camera.zoom directly (focal_length = zoom).
// Fov:  derive focal length from camera.fov_deg and viewport height.
enum class Camera2_5DProjectionMode {
    Zoom, // default — focal length == camera.zoom
    Fov   // derive focal from fov_deg (true FOV-based projection)
};

struct Camera2_5D {
    bool enabled{false};

    // Camera position in screen-space world coordinates.
    // Default: 1000 units "in front" of the z=0 plane.
    Vec3 position{0.0f, 0.0f, -1000.0f};

    // Stored for future two-node camera support; not used for orbit in v1.
    Vec3 point_of_interest{0.0f, 0.0f, 0.0f};
    bool point_of_interest_enabled{false};
    bool hierarchy_baked{false};

    // Projection mode: Zoom (default) or Fov.
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    // Perspective strength. At depth == zoom, perspective_scale == 1.
    // Used when projection_mode == Zoom.
    f32 zoom{1000.0f};

    // Field of view in degrees. Used when projection_mode == Fov.
    // 35° ≈ telephoto (less distortion), 70° ≈ wide angle (more parallax).
    f32 fov_deg{50.0f};

    // Hierarchy
    std::pmr::string parent_name;
    std::pmr::string target_name; // If set, POI is resolved from this layer's world position
    Vec3 rotation{0, 0, 0};

    DepthOfFieldSettings dof;

    [[nodiscard]] Quat rotation_quaternion() const {
        return math::camera_rotation_quat(rotation);
    }

    [[nodiscard]] Vec3 rotation_euler() const {
        return rotation;
    }

    void set_rotation_euler(Vec3 euler_deg) {
        rotation = euler_deg;
    }

    void set_tilt(f32 degrees) {
        rotation.x = degrees;
    }

    void add_tilt(f32 delta_degrees) {
        rotation.x += delta_degrees;
    }

    void set_pan(f32 degrees) {
        rotation.y = degrees;
    }

    void add_pan(f32 delta_degrees) {
        rotation.y += delta_degrees;
    }

    void set_roll(f32 degrees) {
        rotation.z = degrees;
    }

    void add_roll(f32 delta_degrees) {
        rotation.z += delta_degrees;
    }

    [[nodiscard]] Mat4 view_matrix() const {
        const Quat rot = rotation_quaternion();
        if (point_of_interest_enabled && glm::length(point_of_interest - position) > 0.001f) {
            return math::look_at(position, point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
        }
        return math::camera_view_matrix(position, rot);
    }
};

using Camera2_5DRuntime = Camera2_5D;

} // namespace chronon3d
