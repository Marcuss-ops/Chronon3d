#pragma once

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/spring.hpp>
#include <chronon3d/animation/interpolate.hpp>

namespace chronon3d::presets::motion {

/**
 * Renders a clean premium video Progress Bar at the bottom edge.
 */
inline void progress_bar(
    SceneBuilder& s,
    f32 progress,
    Color bar_color,
    const FrameContext& ctx
) {
    f32 bar_height = 8.0f;
    f32 full_width = static_cast<f32>(ctx.width);

    s.screen_layer("progress_bar", [progress, bar_color, bar_height, full_width](LayerBuilder& l) {
        l.position({0.0f, 0.0f, 0.0f});

        // Background track (dark transparent)
        l.rect("track", {
            .size = { full_width, bar_height },
            .color = Color{ 0.1f, 0.1f, 0.15f, 0.5f },
            .pos = { 0.0f, static_cast<f32>(1080.0f * 0.5f - bar_height * 0.5f), 0.0f }
        });

        // Filled progress bar (left-aligned)
        f32 filled_width = full_width * std::clamp(progress, 0.0f, 1.0f);
        l.rect("fill", {
            .size = { filled_width, bar_height },
            .color = bar_color,
            .pos = { - (full_width * 0.5f) + (filled_width * 0.5f), static_cast<f32>(1080.0f * 0.5f - bar_height * 0.5f), 0.0f }
        });
    });
}

} // namespace chronon3d::presets::motion

namespace chronon3d::presets::motion {

inline MotionObject& tilt_sweep_2_5d(
    MotionObject& obj,
    Vec3 position_amplitude = {0.0f, 0.0f, 260.0f},
    Vec3 rotation_amplitude = {11.0f, 15.0f, 6.0f},
    f32 duration_frames = 120.0f,
    f32 start_delay = 0.0f,
    bool one_shot = true
) {
    return obj
        .preset(MotionPreset::TiltSweep2_5D)
        .sweep_2_5d(position_amplitude, rotation_amplitude, duration_frames, start_delay, one_shot);
}

inline MotionObject& fade_lift(MotionObject& obj) {
    return obj.preset(MotionPreset::FadeLift);
}

inline MotionObject& soft_dolly_reveal(MotionObject& obj) {
    return obj
        .preset(MotionPreset::SoftDollyReveal)
        .enable_3d();
}

inline MotionObject& mask_sweep(MotionObject& obj) {
    return obj.preset(MotionPreset::MaskSweep);
}

inline MotionObject& focus_pull(MotionObject& obj) {
    return obj
        .preset(MotionPreset::FocusPull)
        .glow(true);
}

inline MotionObject& perspective_sweep_text_reveal(
    MotionObject& obj
) {
    return obj
        .preset(MotionPreset::PerspectiveSweepTextReveal)
        .enable_3d();
}

} // namespace chronon3d::presets::motion
