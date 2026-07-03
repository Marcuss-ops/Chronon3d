#pragma once

#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <chronon3d/scene/camera/camera_framing.hpp>

namespace chronon3d {

// DEPRECATED (STEP 7): CameraShotProfile is superseded by camera_v1::CameraDescriptor.
// The V1 adapter (camera_descriptor_from(CameraShotProfile)) has been removed.
// Experimental content may still use this struct; migration to CameraDescriptor is recommended.
struct CameraShotProfile {
    CameraRig rig;
    CameraShotValidator validator;
    CameraFramingOptions framing;
    bool auto_fit{false};
    bool emit_report{true};
};

} // namespace chronon3d
