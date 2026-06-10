#pragma once

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>

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
    f32 full_width = static_cast<f32>(ctx.frame.frame.width);

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
        .preset(MotionPreset::SoftDollyReveal);
}

inline MotionObject& dolly_rotate_2_5d(MotionObject& obj) {
    return obj
        .preset(MotionPreset::DollyRotate2_5D)
        .enable_3d();
}

inline MotionObject& glow_bloom(MotionObject& obj) {
    return obj
        .preset(MotionPreset::GlowBloom)
        .glow(true);
}

inline MotionObject& stagger_reveal(MotionObject& obj) {
    return obj.preset(MotionPreset::StaggerReveal);
}

inline MotionObject& mask_sweep(MotionObject& obj) {
    return obj.preset(MotionPreset::MaskSweep);
}

inline MotionObject& focus_pull(MotionObject& obj) {
    return obj.preset(MotionPreset::FocusPull);
}

inline MotionObject& perspective_sweep_text_reveal(
    MotionObject& obj
) {
    return obj
        .preset(MotionPreset::PerspectiveSweepTextReveal)
        .enable_3d();
}

// ── New layer motion presets ────────────────────────────────────────────────

inline MotionObject& slide_in(MotionObject& obj) {
    return obj.preset(MotionPreset::SlideIn);
}

inline MotionObject& soft_pop(MotionObject& obj) {
    return obj.preset(MotionPreset::SoftPop);
}

inline MotionObject& float_idle(MotionObject& obj, f32 parallax = 1.0f) {
    return obj
        .preset(MotionPreset::FloatIdle)
        .parallax(parallax);
}

inline MotionObject& depth_reveal(MotionObject& obj) {
    return obj
        .preset(MotionPreset::DepthReveal)
        .enable_3d();
}

inline MotionObject& card_flip_2_5d(MotionObject& obj) {
    return obj
        .preset(MotionPreset::CardFlip2_5D)
        .enable_3d();
}

inline MotionObject& settle(MotionObject& obj) {
    return obj.preset(MotionPreset::Settle);
}

} // namespace chronon3d::presets::motion
