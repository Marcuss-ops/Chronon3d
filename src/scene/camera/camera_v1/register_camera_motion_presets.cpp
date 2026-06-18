// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_motion_presets.cpp
//
// Phase 1 of the camera subsystem mega-PR. See header for design notes.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_motion_presets.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::camera_v1 {

namespace {

constexpr float kPi = 3.1415926535f;

inline float smoothstep01(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float lerp01(float a, float b, float t) noexcept {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

inline float eval_t(const CameraMotionContext& ctx) noexcept {
    return smoothstep01(
        static_cast<float>(ctx.sample_time.frame) / 90.0f);
}

} // anonymous namespace

// ── DollyMotion ────────────────────────────────────────────────────────────
Camera2_5D DollyMotion::evaluate(const CameraMotionContext& ctx) const {
    const float t = eval_t(ctx);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = { p_.position_xy.x, p_.position_xy.y,
                     lerp01(p_.from_z, p_.to_z, t) };
    cam.zoom = p_.zoom;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = (p_.target.x != 0.0f ||
                                     p_.target.y != 0.0f ||
                                     p_.target.z != 0.0f);
    return cam;
}

// ── PanMotion ──────────────────────────────────────────────────────────────
Camera2_5D PanMotion::evaluate(const CameraMotionContext& ctx) const {
    const float t = eval_t(ctx);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = { lerp01(p_.from_x, p_.to_x, t), 0.0f, p_.z };
    cam.zoom = p_.zoom;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

// ── TiltRollMotion (static pose — t ignored) ───────────────────────────────
Camera2_5D TiltRollMotion::evaluate(const CameraMotionContext& /*ctx*/) const {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = p_.position;
    cam.zoom = p_.zoom;
    cam.rotation = { p_.tilt_deg, p_.pan_deg, p_.roll_deg };
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── OrbitPresetMotion ─────────────────────────────────────────────────────
Camera2_5D OrbitPresetMotion::evaluate(const CameraMotionContext& ctx) const {
    const float t = eval_t(ctx);
    const float angle_rad = lerp01(p_.start_angle_deg, p_.end_angle_deg, t)
                                * (kPi / 180.0f);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {
        std::sin(angle_rad) * p_.radius,
        p_.y,
        p_.z + std::cos(angle_rad) * p_.radius
    };
    cam.zoom = p_.zoom;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

// ── PushInTiltMotion ────────────────────────────────────────────────────────
Camera2_5D PushInTiltMotion::evaluate(const CameraMotionContext& ctx) const {
    const float t = eval_t(ctx);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = { 0.0f, 0.0f, lerp01(p_.from_z, p_.to_z, t) };
    cam.zoom = p_.zoom;
    cam.rotation = { lerp01(p_.from_tilt, p_.to_tilt, t), 0.0f, 0.0f };
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── DollyRotateMotion ──────────────────────────────────────────────────────
Camera2_5D DollyRotateMotion::evaluate(const CameraMotionContext& ctx) const {
    const float t = eval_t(ctx);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = { 0.0f, 0.0f, lerp01(-1280.0f, -820.0f, t) };
    cam.zoom = zoom_;
    cam.rotation = {
        lerp01(-2.5f, 2.5f, t),
        lerp01(-16.0f, 16.0f, t),
        lerp01(-2.0f, 2.0f, t),
    };
    cam.point_of_interest = { 0.0f, 0.0f, 0.0f };
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── RollRevealMotion ───────────────────────────────────────────────────────
Camera2_5D RollRevealMotion::evaluate(const CameraMotionContext& ctx) const {
    const float t = eval_t(ctx);
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = { 0.0f, 0.0f, -1000.0f };
    cam.zoom = zoom_;
    cam.rotation = { 0.0f, 0.0f, lerp01(0.0f, max_roll_deg_, t) };
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── register_camera_motion_presets() — entry point ─────────────────────────
void register_camera_motion_presets() {
    auto& reg = CameraMotionRegistry::instance();
    if (reg.is_frozen()) return;

    using namespace chronon3d::camera_motion;

    if (!reg.has("camera.dolly_in"))
        reg.register_motion(std::make_shared<DollyMotion>(
            DollyParams{ .from_z = -1200.0f, .to_z = -800.0f, .zoom = 1000.0f }));
    if (!reg.has("camera.dolly_out"))
        reg.register_motion(std::make_shared<DollyMotion>(
            DollyParams{ .from_z = -800.0f, .to_z = -1200.0f, .zoom = 1000.0f }));
    if (!reg.has("camera.parallax_sweep"))
        reg.register_motion(std::make_shared<PanMotion>(
            PanParams{ .from_x = -120.0f, .to_x = 120.0f,
                       .z = -1000.0f, .zoom = 1000.0f }));
    if (!reg.has("camera.orbit_small"))
        reg.register_motion(std::make_shared<OrbitPresetMotion>(
            OrbitParams{ .radius = 80.0f, .z = -1000.0f,
                         .start_angle_deg = -15.0f, .end_angle_deg = 15.0f,
                         .zoom = 1000.0f }));
    if (!reg.has("camera.dramatic_push"))
        reg.register_motion(std::make_shared<PushInTiltMotion>(
            PushInTiltParams{ .from_z = -1300.0f, .to_z = -760.0f,
                             .from_tilt = -5.0f, .to_tilt = 5.0f,
                             .zoom = 1000.0f }));
    if (!reg.has("camera.dolly_rotate"))
        reg.register_motion(std::make_shared<DollyRotateMotion>(1000.0f));
    if (!reg.has("camera.roll_reveal"))
        reg.register_motion(std::make_shared<RollRevealMotion>(8.0f, 1000.0f));
}

} // namespace chronon3d::camera_v1
