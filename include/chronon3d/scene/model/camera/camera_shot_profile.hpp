#pragma once

#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <chronon3d/scene/camera/camera_framing.hpp>

namespace chronon3d {

struct CameraShotProfile {
    CameraRig rig;
    CameraShotValidator validator;
    CameraFramingOptions framing;
    bool auto_fit{false};
    bool emit_report{true};
};

} // namespace chronon3d
