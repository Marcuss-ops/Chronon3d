#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include "camera_calibration_scene.hpp"
#include "camera_test_orchestrator.hpp"

namespace chronon3d::content::two_point_five_d {

// ── CameraPitchPositiveTest ───────────────────────────────────────────────
// Pitch +20°: camera looks upward → objects should shift DOWN on screen.
// In Y-down: positive pitch = camera tilts up → horizon drops → floor visible.
Composition camera_pitch_positive_test() {
    return composition({.name = "CameraPitchPositiveTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s, false); // no pillars, just card + grid

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(0.0f);
        shot.rig.orbit_pitch.set(20.0f);   // +20° pitch
        shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .require_visible("calibration_card", 0.40f);

        return camera_test_orchestrator(ctx, s, shot, "CameraPitchPositiveTest",
            {"calibration_card"}, {0});
    });
}

// ── CameraPitchNegativeTest ───────────────────────────────────────────────
// Pitch -20°: camera looks downward → objects should shift UP on screen.
// In Y-down: negative pitch = camera tilts down → horizon rises → ceiling visible.
Composition camera_pitch_negative_test() {
    return composition({.name = "CameraPitchNegativeTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s, false);

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(0.0f);
        shot.rig.orbit_pitch.set(-20.0f);  // -20° pitch
        shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .require_visible("calibration_card", 0.40f);

        return camera_test_orchestrator(ctx, s, shot, "CameraPitchNegativeTest",
            {"calibration_card"}, {0});
    });
}

} // namespace chronon3d::content::two_point_five_d
