#pragma once

#include <chronon3d/animations/camera_motion_easing.hpp>

#include <string>

namespace chronon3d::animation {

enum class MotionAxis {
    Tilt,
    Pan,
    Roll,
};

struct CameraMotionPose {
    Vec3 position{0.0f, 0.0f, -1080.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    f32 zoom{1080.0f};
};

struct CameraMotionPrimary {
    CameraMotionPose from{};
    CameraMotionPose to{};
    Frame duration{0};
    Easing easing{Easing::Smoothstep};
    bool enabled{false};
};

struct CameraMotionIdle {
    bool enabled{false};
    Vec3 position_amplitude{0.0f, 0.0f, 0.0f};
    Vec3 rotation_amplitude_deg{0.0f, 0.0f, 0.0f};
    f32 zoom_amplitude{0.0f};
    f32 frequency_hz{0.15f};
    f32 phase_offset{0.0f};
    bool base_on_final{true};
};

struct CameraMotionParams {
    MotionAxis axis{MotionAxis::Tilt};
    f32 start_deg{-18.0f};
    f32 end_deg{18.0f};
    Frame duration{60};
    Frame start_frame{0};
    int width{1920};
    int height{1080};
    Vec3 position{0.0f, 0.0f, -1080.0f};
    f32 zoom{1080.0f};
    CameraMotionPose pose{};
    CameraMotionPrimary primary{};
    CameraMotionIdle idle{};
    std::string reference_image{"assets/images/camera_reference.jpg"};
};

} // namespace chronon3d::animation
