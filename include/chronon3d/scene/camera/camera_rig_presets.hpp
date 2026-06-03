#pragma once

#include <chronon3d/scene/camera/camera_rig.hpp>
#include <string>

namespace chronon3d::camera_rig_presets {

[[nodiscard]] CameraRig orbit_reveal(std::string target = "camera_target");

[[nodiscard]] CameraRig premium_push_in(std::string target = "camera_target");

} // namespace chronon3d::camera_rig_presets
