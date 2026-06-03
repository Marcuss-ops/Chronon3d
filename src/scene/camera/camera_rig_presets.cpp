#include <chronon3d/scene/camera/camera_rig_presets.hpp>

namespace chronon3d::camera_rig_presets {

CameraRig orbit_reveal(std::string target) {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_name = std::move(target);

    rig.orbit_yaw.key(0, -18.0f)
                 .key(90, 18.0f, EasingCurve{Easing::InOutCubic});

    rig.orbit_radius.key(0, 1250.0f)
                    .key(90, 850.0f, EasingCurve{Easing::OutCubic});

    rig.roll.key(0, -2.0f)
            .key(90, 0.0f, EasingCurve{Easing::OutCubic});

    rig.fov_deg.set(50.0f);
    rig.projection_mode = Camera2_5DProjectionMode::Fov;

    return rig;
}

CameraRig premium_push_in(std::string target) {
    CameraRig rig;
    rig.mode = CameraRigMode::TwoNode;
    rig.target_name = std::move(target);

    rig.orbit_radius.key(0, 1400.0f)
                    .key(120, 900.0f, EasingCurve{Easing::OutCubic});

    rig.orbit_pitch.key(0, -4.0f)
                   .key(120, 2.0f, EasingCurve{Easing::InOutCubic});

    rig.roll.key(0, -1.5f)
            .key(120, 0.0f);

    rig.projection_mode = Camera2_5DProjectionMode::Fov;
    rig.fov_deg.set(45.0f);

    return rig;
}

} // namespace chronon3d::camera_rig_presets
