#pragma once

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
        return Mat4::perspective(fov_deg * 3.14159f / 180.0f, aspect_ratio, near_plane, far_plane);
    }

    [[nodiscard]] Mat4 view_matrix() const {
        // View matrix is inverse of camera transform
        // For now, simple translation. In future, implement proper LookAt or full inverse.
        return Mat4::translate(transform.position * -1.0f) * Mat4::rotate(transform.rotation).identity(); 
        // Note: Full inverse rotation needed for correct camera behavior.
    }
};

} // namespace chronon3d
