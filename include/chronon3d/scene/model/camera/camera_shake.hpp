#pragma once

#include <chronon3d/animation/wiggle.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

// ── CameraShakeConfig — configurable camera shake ────────────────────────────
//
// Applies wiggle-based shake to a Camera2_5D. Use ShakePreset::* for common
// configurations, or customize directly.
//
// Usage:
//   CameraShakeConfig cfg = ShakePreset::handheld(0.8f);
//   cfg.apply_to(camera, frame, total_duration);

struct CameraShakeConfig {
    f32 position_freq{3.0f};       // Hz — position oscillation frequency
    f32 position_amp{15.0f};       // units — max position displacement
    f32 rotation_freq{2.5f};       // Hz — rotation oscillation frequency
    f32 rotation_amp{2.0f};        // degrees — max rotation displacement
    f32 zoom_freq{1.0f};           // Hz — zoom oscillation frequency
    f32 zoom_amp{0.0f};            // units — max zoom displacement (0 = no zoom shake)
    u32 seed{42};                  // random seed for determinism
    f32 fade_in_frames{0.0f};      // frames — gradual fade-in from 0 amplitude
    f32 fade_out_frames{0.0f};     // frames — gradual fade-out to 0 amplitude

    // Apply shake to a Camera2_5D at a given frame within a duration.
    // Modifies position, rotation, and zoom in-place.
    void apply_to(Camera2_5D& cam, Frame frame, Frame total_duration = Frame{300}) const {
        const f32 t = static_cast<f32>(frame);

        // Compute fade envelope
        f32 envelope = 1.0f;
        if (fade_in_frames > 0.0f && t < fade_in_frames) {
            envelope *= t / fade_in_frames;
        }
        const f32 remaining = static_cast<f32>(total_duration - frame);
        if (fade_out_frames > 0.0f && remaining < fade_out_frames) {
            envelope *= remaining / fade_out_frames;
        }
        envelope = std::clamp(envelope, 0.0f, 1.0f);

        if (envelope <= 0.0f) return;

        // Position shake
        const Vec3 pos_offset = wiggle3D(position_freq, position_amp * envelope, t, seed);
        cam.position += pos_offset;

        // Rotation shake
        const Vec3 rot_offset = wiggle3D(rotation_freq, rotation_amp * envelope, t, seed + 50u);
        cam.rotation += rot_offset;

        // Zoom shake
        if (zoom_amp > 0.0f) {
            cam.zoom += wiggle(zoom_freq, zoom_amp * envelope, t, seed + 150u);
        }
    }

    // Apply shake to an AnimatedCamera2_5D by adding keyframes for the shake
    // displacement at regular intervals. Useful for baking shake into keyframes.
    void bake_into(AnimatedCamera2_5D& cam, Frame start, Frame end, Frame step = Frame{1}) const {
        for (Frame f = start; f <= end; f += step) {
            Camera2_5D base = cam.evaluate(f);
            apply_to(base, f - start, end - start);
            cam.position.key(f, base.position);
            cam.rotation.key(f, base.rotation);
            if (zoom_amp > 0.0f) {
                cam.zoom.key(f, base.zoom);
            }
        }
    }
};

// ── Shake Presets ────────────────────────────────────────────────────────────

namespace ShakePreset {

// Subtle handheld camera shake — like holding a DSLR
inline CameraShakeConfig handheld(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 4.0f;
    cfg.position_amp = 3.0f * intensity;
    cfg.rotation_freq = 3.0f;
    cfg.rotation_amp = 0.5f * intensity;
    cfg.seed = 42;
    return cfg;
}

// Earthquake / impact shake — violent, fast, decaying
inline CameraShakeConfig earthquake(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 12.0f;
    cfg.position_amp = 25.0f * intensity;
    cfg.rotation_freq = 10.0f;
    cfg.rotation_amp = 5.0f * intensity;
    cfg.zoom_freq = 8.0f;
    cfg.zoom_amp = 10.0f * intensity;
    cfg.fade_out_frames = 30.0f;
    cfg.seed = 77;
    return cfg;
}

// Gentle drift / wind — slow, organic sway
inline CameraShakeConfig drift(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 0.3f;
    cfg.position_amp = 8.0f * intensity;
    cfg.rotation_freq = 0.2f;
    cfg.rotation_amp = 1.0f * intensity;
    cfg.zoom_freq = 0.15f;
    cfg.zoom_amp = 5.0f * intensity;
    cfg.seed = 123;
    return cfg;
}

// Handheld walking — rhythmic bob + sway
inline CameraShakeConfig walking(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 2.0f;
    cfg.position_amp = 6.0f * intensity;
    cfg.rotation_freq = 2.0f;
    cfg.rotation_amp = 1.5f * intensity;
    cfg.seed = 99;
    return cfg;
}

// Breathing — very subtle, slow oscillation
inline CameraShakeConfig breathing(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 0.25f;
    cfg.position_amp = 2.0f * intensity;
    cfg.rotation_freq = 0.25f;
    cfg.rotation_amp = 0.3f * intensity;
    cfg.seed = 55;
    return cfg;
}

// Explosion impact — starts violent, fades out quickly
inline CameraShakeConfig explosion(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 15.0f;
    cfg.position_amp = 40.0f * intensity;
    cfg.rotation_freq = 12.0f;
    cfg.rotation_amp = 8.0f * intensity;
    cfg.zoom_freq = 10.0f;
    cfg.zoom_amp = 15.0f * intensity;
    cfg.fade_out_frames = 20.0f;
    cfg.seed = 33;
    return cfg;
}

// Cinematic subtle — very gentle, barely perceptible
inline CameraShakeConfig cinematic(f32 intensity = 1.0f) {
    CameraShakeConfig cfg;
    cfg.position_freq = 1.5f;
    cfg.position_amp = 1.5f * intensity;
    cfg.rotation_freq = 1.0f;
    cfg.rotation_amp = 0.2f * intensity;
    cfg.seed = 200;
    return cfg;
}

} // namespace ShakePreset

} // namespace chronon3d
