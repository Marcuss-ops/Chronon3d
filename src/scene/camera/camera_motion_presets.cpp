// ==============================================================================
// chronon3d/src/scene/camera/camera_motion_presets.cpp
//
// After Phase 1 of the camera subsystem mega-PR, this file becomes a thin
// delegating façade over the data-driven RegisteredMotion subclasses in
// src/scene/camera/camera_v1/register_camera_motion_presets.cpp. The 12
// camera_motion::* functions keep identical signatures so every caller in
// content/ and tests continues to compile unchanged.
// ==============================================================================
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/camera/camera_v1/register_camera_motion_presets.hpp>

namespace chronon3d::camera_motion {

// ── Easing helpers DEMOTED ─────────────────────────────────────────────────
//
// smoothstep01 / lerp01 / clamp01 now live inside the registered motion
// classes (single source of truth). The free functions exposed in the header
// remain as inline shims (defined in the V1 implementation header) so legacy
// callers compile, but new code should not use them.
inline float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float clamp01(float t) {
    return std::clamp(t, 0.0f, 1.0f);
}
inline float lerp(float a, float b, float t) {
    return a + (b - a) * clamp01(t);
}

// ── Parametric fns ─────────────────────────────────────────────────────────

Camera2_5D dolly(float t, const DollyParams& p) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::DollyMotion(p)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(t));
}

Camera2_5D pan(float t, const PanParams& p) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::PanMotion(p)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(t));
}

Camera2_5D tilt_roll(float /*t*/, const TiltRollParams& p) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::TiltRollMotion(p)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(0.0f));
}

Camera2_5D orbit(float t, const OrbitParams& p) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::OrbitPresetMotion(p)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(t));
}

Camera2_5D push_in_tilt(float t, const PushInTiltParams& p) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::PushInTiltMotion(p)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(t));
}

// ── Convenience one-liners ────────────────────────────────────────────────

Camera2_5D dolly_in(float t, float zoom) {
    return dolly(t, DollyParams{ .from_z = -1200.0f, .to_z = -800.0f, .zoom = zoom });
}

Camera2_5D dolly_out(float t, float zoom) {
    return dolly(t, DollyParams{ .from_z = -800.0f, .to_z = -1200.0f, .zoom = zoom });
}

Camera2_5D parallax_sweep(float t, float amplitude, float z, float zoom) {
    return pan(t, PanParams{ .from_x = -amplitude, .to_x = amplitude,
                              .z = z, .zoom = zoom });
}

Camera2_5D orbit_small(float t, float zoom) {
    return orbit(t, OrbitParams{
        .radius = 80.0f, .z = -1000.0f,
        .start_angle_deg = -15.0f, .end_angle_deg = 15.0f,
        .zoom = zoom
    });
}

Camera2_5D dramatic_push(float t, float zoom) {
    return push_in_tilt(t, PushInTiltParams{
        .from_z = -1300.0f, .to_z = -760.0f,
        .from_tilt = -5.0f, .to_tilt = 5.0f,
        .zoom = zoom
    });
}

Camera2_5D dolly_rotate(float t, float zoom) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::DollyRotateMotion(zoom)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(t));
}

Camera2_5D roll_reveal(float t, float max_roll_deg, float zoom) {
    chronon3d::camera_v1::register_camera_v1_builtins();
    return chronon3d::camera_v1::RollRevealMotion(max_roll_deg, zoom)
        .evaluate(chronon3d::camera_v1::motion_ctx_from_t(t));
}

// apply_dolly_pitch_sweep was a SceneBuilder side-effecting helper that
// combined camera + layer animation. With camera motion fully data-driven,
// callers compose via SceneBuilder directly — kept for one release as a
// pass-through stub before removal.
void apply_dolly_pitch_sweep(SceneBuilder& /*s*/, LayerBuilder& /*l*/, int /*duration*/) {
    // deprecated; will be removed in a follow-up.
}

} // namespace chronon3d::camera_motion
