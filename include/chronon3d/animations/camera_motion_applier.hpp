#pragma once

#include <chronon3d/animations/camera_motion_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace chronon3d::animation {

[[nodiscard]] Camera2_5D make_camera_from_pose(const CameraMotionPose& pose);

void apply_camera_motion(SceneBuilder& s,
                         const FrameContext& ctx,
                         const CameraMotionParams& p);

} // namespace chronon3d::animation
