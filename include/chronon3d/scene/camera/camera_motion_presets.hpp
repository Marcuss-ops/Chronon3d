#pragma once

#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/vec3.hpp>

// Camera motion presets for 2.5D animation.
// All preset functions take t ∈ [0,1] (normalised frame progress).
// Call camera_motion::smoothstep(t) is already applied internally.
//
// Usage:
//   const float t = ctx.frame / float(ctx.duration - 1);
//   s.camera().set(camera_motion::orbit(t, {.radius=220, .target={0,0,0}}));

namespace chronon3d::camera_motion {

// ── Easing helpers ────────────────────────────────────────────────────────────

float clamp01(float t);
float smoothstep(float t);
float lerp(float a, float b, float t);

// ── Param structs ─────────────────────────────────────────────────────────────

struct DollyParams {
    float from_z{-1200.0f};
    float to_z{-800.0f};
    Vec3  position_xy{0.0f, 0.0f, 0.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    float zoom{1000.0f};
};

struct PanParams {
    float from_x{-120.0f};
    float to_x{120.0f};
    float z{-1000.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    float zoom{1000.0f};
};

struct TiltRollParams {
    Vec3  position{0.0f, 0.0f, -1000.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    float tilt_deg{0.0f};
    float pan_deg{0.0f};
    float roll_deg{0.0f};
    float zoom{1000.0f};
};

struct OrbitParams {
    float radius{180.0f};
    float z{-1000.0f};
    float y{0.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    float start_angle_deg{-20.0f};
    float end_angle_deg{20.0f};
    float zoom{1000.0f};
};

struct PushInTiltParams {
    float from_z{-1200.0f};
    float to_z{-850.0f};
    float from_tilt{-4.0f};
    float to_tilt{4.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    float zoom{1000.0f};
};

// ── Preset functions ──────────────────────────────────────────────────────────

Camera2_5D dolly(float t, const DollyParams& p = {});
Camera2_5D pan(float t, const PanParams& p = {});
Camera2_5D tilt_roll(float t, const TiltRollParams& p = {});
Camera2_5D orbit(float t, const OrbitParams& p = {});
Camera2_5D push_in_tilt(float t, const PushInTiltParams& p = {});

// ── Convenience one-liners ────────────────────────────────────────────────────

// Camera dollies in from -1200 to -800 with smoothstep easing.
Camera2_5D dolly_in(float t, float zoom = 1000.0f);

// Camera dollies out from -800 to -1200.
Camera2_5D dolly_out(float t, float zoom = 1000.0f);

// Pan left-to-right while looking at origin — great for parallax shots.
Camera2_5D parallax_sweep(float t, float amplitude = 120.0f, float z = -1000.0f, float zoom = 1000.0f);

// Tight orbit: ±15° around Y axis — classic AE card reveal.
Camera2_5D orbit_small(float t, float zoom = 1000.0f);

// Dramatic push-in with simultaneous tilt: -5° → +5°.
Camera2_5D dramatic_push(float t, float zoom = 1000.0f);

// Subtle roll: camera tilts 0° → 8° counterclockwise (cinematic handover).
Camera2_5D roll_reveal(float t, float max_roll_deg = 8.0f, float zoom = 1000.0f);

} // namespace chronon3d::camera_motion
