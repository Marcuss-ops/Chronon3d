#pragma once

#include <chronon3d/animations/camera_motion_applier.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <functional>
#include <string>

namespace chronon3d::presets {

using ContentBuilder = std::function<void(SceneBuilder&, const FrameContext&, const animation::CameraMotionParams&)>;

Composition camera_motion_clip(std::string name,
                               animation::CameraMotionParams params,
                               ContentBuilder content_builder);

} // namespace chronon3d::presets
