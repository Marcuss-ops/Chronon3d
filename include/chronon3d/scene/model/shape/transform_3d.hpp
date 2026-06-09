#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <glm/gtx/quaternion.hpp>
#include <string>

namespace chronon3d {

struct Transform3D {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f}; // in degrees
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 anchor{0.0f, 0.0f, 0.0f};
    std::string parent_name;
    bool inherits_position{true};
    bool inherits_rotation{true};
    bool inherits_scale{true};

    [[nodiscard]] Mat4 to_mat4() const {
        Quat rot_quat = glm::quat(glm::radians(rotation));
        return glm::translate(Mat4(1.0f), position) *
               glm::toMat4(rot_quat) *
               glm::scale(Mat4(1.0f), scale) *
               glm::translate(Mat4(1.0f), -anchor);
    }
};

} // namespace chronon3d
