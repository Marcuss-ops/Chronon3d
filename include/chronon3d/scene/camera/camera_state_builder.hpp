#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <string>
#include <utility>

namespace chronon3d {

/// Convenience value builder for a renderer-ready Camera2_5D state.
/// It does not evaluate animation, constraints, presets, or camera motion.
class CameraStateBuilder {
public:
    CameraStateBuilder& enabled(bool value = true) noexcept {
        camera_.enabled = value;
        return *this;
    }
    CameraStateBuilder& position(Vec3 value) noexcept {
        camera_.position = value;
        return *this;
    }
    CameraStateBuilder& rotation(Vec3 value) noexcept {
        camera_.rotation = value;
        return *this;
    }
    CameraStateBuilder& zoom(f32 value) noexcept {
        camera_.optics_mode = CameraOpticsMode::Zoom;
        camera_.zoom = value;
        return *this;
    }
    CameraStateBuilder& fov(f32 value) noexcept {
        camera_.optics_mode = CameraOpticsMode::FieldOfView;
        camera_.fov_deg = value;
        return *this;
    }
    CameraStateBuilder& parent(std::string value) {
        camera_.parent_name = std::move(value);
        return *this;
    }
    CameraStateBuilder& target(std::string value) {
        camera_.target_name = std::move(value);
        camera_.point_of_interest_enabled = true;
        return *this;
    }
    CameraStateBuilder& point_of_interest(Vec3 value) noexcept {
        camera_.point_of_interest_enabled = true;
        camera_.point_of_interest = value;
        return *this;
    }
    CameraStateBuilder& dof(DepthOfFieldSettings value) noexcept {
        camera_.dof = value;
        return *this;
    }
    CameraStateBuilder& look_at(Vec3 value) noexcept {
        return point_of_interest(value);
    }
    [[nodiscard]] Camera2_5D build() const { return camera_; }

private:
    Camera2_5D camera_{};
};

inline CameraStateBuilder camera() { return {}; }

} // namespace chronon3d
