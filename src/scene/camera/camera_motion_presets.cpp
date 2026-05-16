#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::camera_motion {

// ── Easing helpers ────────────────────────────────────────────────────────────

float clamp01(float t) {
    return std::clamp(t, 0.0f, 1.0f);
}

float smoothstep(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * clamp01(t);
}

// ── Preset implementations ────────────────────────────────────────────────────

Camera2_5D dolly(float t, const DollyParams& p) {
    t = smoothstep(t);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {p.position_xy.x, p.position_xy.y, lerp(p.from_z, p.to_z, t)};
    cam.zoom = p.zoom;
    cam.point_of_interest = p.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

Camera2_5D pan(float t, const PanParams& p) {
    t = smoothstep(t);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {lerp(p.from_x, p.to_x, t), 0.0f, p.z};
    cam.zoom = p.zoom;
    cam.point_of_interest = p.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

Camera2_5D tilt_roll(float /*t*/, const TiltRollParams& p) {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = p.position;
    cam.zoom = p.zoom;
    cam.rotation = {p.tilt_deg, p.pan_deg, p.roll_deg};
    cam.point_of_interest = p.target;
    cam.point_of_interest_enabled = false;
    return cam;
}

Camera2_5D orbit(float t, const OrbitParams& p) {
    t = smoothstep(t);
    const float angle_rad = (lerp(p.start_angle_deg, p.end_angle_deg, t)) * (3.14159265f / 180.0f);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {std::sin(angle_rad) * p.radius, p.y, p.z + std::cos(angle_rad) * p.radius};
    cam.zoom = p.zoom;
    cam.point_of_interest = p.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

Camera2_5D push_in_tilt(float t, const PushInTiltParams& p) {
    t = smoothstep(t);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, lerp(p.from_z, p.to_z, t)};
    cam.zoom = p.zoom;
    cam.rotation = {lerp(p.from_tilt, p.to_tilt, t), 0.0f, 0.0f};
    cam.point_of_interest = p.target;
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── Convenience one-liners ────────────────────────────────────────────────────

Camera2_5D dolly_in(float t, float zoom) {
    return dolly(t, {.from_z = -1200.0f, .to_z = -800.0f, .zoom = zoom});
}

Camera2_5D dolly_out(float t, float zoom) {
    return dolly(t, {.from_z = -800.0f, .to_z = -1200.0f, .zoom = zoom});
}

Camera2_5D parallax_sweep(float t, float amplitude, float z, float zoom) {
    return pan(t, {.from_x = -amplitude, .to_x = amplitude, .z = z, .zoom = zoom});
}

Camera2_5D orbit_small(float t, float zoom) {
    return orbit(t, {.radius = 80.0f, .z = -1000.0f, .start_angle_deg = -15.0f, .end_angle_deg = 15.0f, .zoom = zoom});
}

Camera2_5D dramatic_push(float t, float zoom) {
    return push_in_tilt(t, {.from_z = -1300.0f, .to_z = -760.0f, .from_tilt = -5.0f, .to_tilt = 5.0f, .zoom = zoom});
}

Camera2_5D roll_reveal(float t, float max_roll_deg, float zoom) {
    t = smoothstep(t);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = zoom;
    cam.rotation = {0.0f, 0.0f, lerp(0.0f, max_roll_deg, t)};
    cam.point_of_interest_enabled = false;
    return cam;
}

} // namespace chronon3d::camera_motion
