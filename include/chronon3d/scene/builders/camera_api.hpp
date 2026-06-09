#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <string>

namespace chronon3d {

// Forward declaration
class SceneBuilder;

class CameraApi {
public:
    explicit CameraApi(SceneBuilder& owner)
        : owner_(&owner) {}

    CameraApi& set(Camera2_5D camera);
    CameraApi& set_animated(const AnimatedCamera2_5D& cam);
    CameraApi& enable(bool enabled = true);
    CameraApi& position(Vec3 p);
    CameraApi& zoom(f32 value);
    CameraApi& fov(f32 fov_deg);
    CameraApi& dof(DepthOfFieldSettings value);
    CameraApi& parent(std::string name);
    CameraApi& rotation(Vec3 euler_deg);
    CameraApi& tilt(f32 degrees);
    CameraApi& pan(f32 degrees);
    CameraApi& roll(f32 degrees);
    CameraApi& point_of_interest(Vec3 poi);
    CameraApi& look_at(Vec3 poi);
    CameraApi& target(std::string name);

private:
    SceneBuilder* owner_;
};

} // namespace chronon3d
