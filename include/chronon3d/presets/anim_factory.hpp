#pragma once

#include <chronon3d/presets/motion_animation.hpp>

namespace chronon3d::presets::motion::anim {

inline MotionAnimation fade_in(Frame start = 0, Frame end = 20, EasingCurve easing = Easing::Linear) {
    return MotionAnimation(start, end, easing, [](MotionState& state, f32 progress) {
        state.opacity = progress;
    });
}

inline MotionAnimation up(f32 distance = 80.0f, Frame start = 0, Frame end = 30, EasingCurve easing = Easing::OutCubic) {
    return MotionAnimation(start, end, easing, [distance](MotionState& state, f32 progress) {
        state.position.y += (1.0f - progress) * distance;
    });
}

inline MotionAnimation blur_in(f32 max_blur = 12.0f, Frame start = 0, Frame end = 25, EasingCurve easing = Easing::OutCubic) {
    return MotionAnimation(start, end, easing, [max_blur](MotionState& state, f32 progress) {
        state.blur = (1.0f - progress) * max_blur;
    });
}

inline MotionAnimation spring_up(f32 distance = 100.0f, Frame start = 0, f32 mass = 1.0f, f32 stiffness = 100.0f, f32 damping = 10.0f) {
    Frame end = start + 60;
    return MotionAnimation(start, end, Easing::Linear, [distance, start, end, mass, stiffness, damping](MotionState& state, f32 progress) {
        f32 t = progress * (static_cast<f32>(end - start) / 30.0f);
        f32 spring_value = compute_spring(t, mass, stiffness, damping);
        state.position.y += (1.0f - spring_value) * distance;
    });
}

inline MotionAnimation push_in_3d(f32 start_z = -500.0f, Frame start = 0, Frame end = 40, EasingCurve easing = Easing::OutCubic) {
    return MotionAnimation(start, end, easing, [start_z](MotionState& state, f32 progress) {
        state.position.z += (1.0f - progress) * start_z;
    });
}

inline MotionAnimation flip_y(f32 start_degrees = -90.0f, Frame start = 0, Frame end = 30, EasingCurve easing = Easing::OutQuad) {
    return MotionAnimation(start, end, easing, [start_degrees](MotionState& state, f32 progress) {
        state.rotation.y += (1.0f - progress) * start_degrees;
    });
}

inline MotionAnimation shake_3d(f32 intensity = 5.0f, Frame start = 0, Frame end = 60) {
    return MotionAnimation(start, end, Easing::Linear, [intensity, start, end](MotionState& state, f32 progress) {
        f32 frequency = 20.0f;
        f32 decay = 1.0f - progress;
        state.rotation.x += std::sin(progress * frequency) * intensity * decay;
        state.rotation.y += std::cos(progress * frequency * 0.8f) * intensity * decay;
    });
}

} // namespace chronon3d::presets::motion::anim
