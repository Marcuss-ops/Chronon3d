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
        return math::perspective(glm::radians(fov_deg), aspect_ratio, near_plane, far_plane);
    }

    [[nodiscard]] Mat4 view_matrix() const {
        // View matrix is inverse of camera transform
        return glm::inverse(transform.to_matrix());
    }
};

} // namespace chronon3d
