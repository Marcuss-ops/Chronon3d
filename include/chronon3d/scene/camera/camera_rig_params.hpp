#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_rig_params.hpp
//
// Parameter structs for the camera_rig convenience helpers (hero_push_in, etc.).
// Extracted from camera_rig.hpp to break circular dependency with
// register_camera_rig_motions.hpp.
//
// These are pure data structs — no evaluation logic, just parameter containers.
// ==============================================================================
#include <chronon3d/animation/easing/easing.hpp>   // EasingCurve
#include <chronon3d/animation/core/animated_value.hpp>  // AnimatedValue
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d::camera_rig {

struct HeroPushInParams {
    Vec3  from_position{0.0f, 0.0f, -1200.0f};
    Vec3  to_position{0.0f, 0.0f, -750.0f};
    f32   from_tilt{-4.0f};
    f32   to_tilt{2.0f};
    f32   from_yaw{0.0f};
    f32   to_yaw{3.0f};
    f32   zoom{1000.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{EasingCurve::cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f)};
};

struct OrbitYawParams {
    Vec3  target{0.0f, 0.0f, 0.0f};
    f32   radius{300.0f};
    f32   height{40.0f};
    f32   z_offset{-1000.0f};
    f32   start_angle_deg{-25.0f};
    f32   end_angle_deg{25.0f};
    f32   tilt_deg{-5.0f};
    f32   zoom{1000.0f};
    Frame duration{120};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutCubic};
};

struct ParallaxPanParams {
    Vec3  from_position{-180.0f, 0.0f, -1000.0f};
    Vec3  to_position{180.0f, 0.0f, -1000.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    f32   zoom{1000.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutSine};
};

struct DollyZoomParams {
    Vec3  from_position{0.0f, 0.0f, -1200.0f};
    Vec3  to_position{0.0f, 0.0f, -600.0f};
    f32   from_zoom{1200.0f};
    f32   to_zoom{600.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutCubic};
};

struct FocusPullParams {
    Vec3  position{0.0f, 0.0f, -1000.0f};
    f32   zoom{1000.0f};
    f32   from_focus_z{0.0f};
    f32   to_focus_z{500.0f};
    f32   aperture{0.03f};
    f32   max_blur{24.0f};
    Frame duration{60};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutCubic};
};

struct LowAngleRevealParams {
    Vec3  from_position{0.0f, -180.0f, -1100.0f};
    Vec3  to_position{0.0f, 40.0f, -800.0f};
    f32   from_tilt{25.0f};
    f32   to_tilt{0.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    f32   zoom{1000.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{EasingCurve::cubic_bezier(0.33f, 1.0f, 0.68f, 1.0f)};
};

struct SubtleFloatParams {
    Vec3  base_position{0.0f, 0.0f, -1000.0f};
    f32   x_amplitude{15.0f};
    f32   y_amplitude{8.0f};
    f32   z_amplitude{20.0f};
    f32   x_frequency{0.3f};
    f32   y_frequency{0.2f};
    f32   z_frequency{0.15f};
    f32   zoom{1000.0f};
    Frame duration{300};
    Frame start_frame{0};
};

} // namespace chronon3d::camera_rig
