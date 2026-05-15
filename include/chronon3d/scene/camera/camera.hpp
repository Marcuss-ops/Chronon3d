#pragma once

#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {

class Camera {
public:
    f32 fov_deg{60.0f};
    f32 near_plane{0.1f};
    f32 far_plane{1000.0f};
    Transform transform;

    [[nodiscard]] Mat4 projection_matrix(f32 aspect_ratio) const {
        return math::perspective(glm::radians(fov_deg), aspect_ratio, near_plane, far_plane);
    }

    [[nodiscard]] Mat4 view_matrix() const {
        return math::camera_view_matrix(transform.position, transform.rotation);
    }

    [[nodiscard]] Vec3 rotation_euler() const {
        return math::camera_rotation_euler(transform.rotation);
    }

    void set_rotation_euler(Vec3 euler_deg) {
        transform.rotation = math::camera_rotation_quat(euler_deg);
    }

    void set_tilt(f32 degrees) {
        Vec3 euler = rotation_euler();
        euler.x = degrees;
        set_rotation_euler(euler);
    }

    void add_tilt(f32 delta_degrees) {
        Vec3 euler = rotation_euler();
        euler.x += delta_degrees;
        set_rotation_euler(euler);
    }

    void set_pan(f32 degrees) {
        Vec3 euler = rotation_euler();
        euler.y = degrees;
        set_rotation_euler(euler);
    }

    void add_pan(f32 delta_degrees) {
        Vec3 euler = rotation_euler();
        euler.y += delta_degrees;
        set_rotation_euler(euler);
    }

    void set_roll(f32 degrees) {
        Vec3 euler = rotation_euler();
        euler.z = degrees;
        set_rotation_euler(euler);
    }

    void add_roll(f32 delta_degrees) {
        Vec3 euler = rotation_euler();
        euler.z += delta_degrees;
        set_rotation_euler(euler);
    }

    [[nodiscard]] f32 focal_length(f32 width) const {
        return (width * 0.5f) / std::tan(glm::radians(fov_deg) * 0.5f);
    }
};

} // namespace chronon3d
