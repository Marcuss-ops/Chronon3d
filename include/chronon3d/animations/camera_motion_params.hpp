#pragma once

#include <chronon3d/animation/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace chronon3d::animation {

using Easing = chronon3d::Easing;

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

inline f32 lerp(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, f32 t) {
    return a + (b - a) * t;
}

inline f32 smoothstep(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline f32 normalized_time(Frame frame, Frame duration) {
    const Frame span = std::max<Frame>(1, duration - 1);
    return smoothstep(static_cast<f32>(frame) / static_cast<f32>(span));
}

inline f32 easing_value(Easing easing, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::OutCubic: {
        const f32 u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case Easing::InOutCubic:
        return (t < 0.5f)
            ? 4.0f * t * t * t
            : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case Easing::Smoothstep:
    default:
        return smoothstep(t);
    }
}

} // namespace chronon3d::animation
