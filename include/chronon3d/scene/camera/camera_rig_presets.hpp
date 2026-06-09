#pragma once

#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <string>

namespace chronon3d::camera_rig_presets {

[[nodiscard]] CameraRig orbit_reveal(std::string target = "camera_target");

[[nodiscard]] CameraRig premium_push_in(std::string target = "camera_target");

[[nodiscard]] CameraRig parallax_stack(std::string target = "camera_target");

[[nodiscard]] CameraRig slow_dolly_focus(std::string target = "camera_target");

[[nodiscard]] CameraRig card_fan_sweep(std::string target = "camera_target");

[[nodiscard]] CameraRig hero_title_push(std::string target = "camera_target");

} // namespace chronon3d::camera_rig_presets

