// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_rig_motions.cpp
//
// Phase 2 implementations. See header for design notes.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/register_camera_rig_motions.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::camera_v1 {

namespace {

inline float easing_apply(const chronon3d::EasingCurve& e, float t) noexcept {
    // Mirror EasingCurve::apply(float) without taking a dependency on the
    // header chain. Cheap fallback: linear if direct call fails.
    return e.apply(std::clamp(t, 0.0f, 1.0f));
}

inline float parametric_t(const CameraMotionContext& ctx, i32 duration) noexcept {
    if (duration <= 0) return 0.0f;
    const float raw = static_cast<float>(ctx.sample_time.frame) /
                      static_cast<float>(duration);
    return std::clamp(raw, 0.0f, 1.0f);
}

inline Camera2_5D empty_cam() {
    Camera2_5D cam;
    cam.enabled = false;
    return cam;
}

} // anonymous namespace

// ── HeroPushInMotion ───────────────────────────────────────────────────────
Camera2_5D HeroPushInMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float t = easing_apply(p_.easing,
                                 parametric_t(ctx, static_cast<i32>(p_.duration)));
    cam.position = {
        p_.from_position.x + (p_.to_position.x - p_.from_position.x) * t,
        p_.from_position.y + (p_.to_position.y - p_.from_position.y) * t,
        p_.from_position.z + (p_.to_position.z - p_.from_position.z) * t,
    };
    cam.rotation = {
        p_.from_tilt   + (p_.to_tilt   - p_.from_tilt)   * t,
        p_.from_yaw    + (p_.to_yaw    - p_.from_yaw)    * t,
        0.0f,
    };
    cam.zoom = p_.zoom;
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── OrbitYawMotion ─────────────────────────────────────────────────────────
Camera2_5D OrbitYawMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float t = easing_apply(p_.easing,
                                 parametric_t(ctx, static_cast<i32>(p_.duration)));
    const float start_rad = glm::radians(p_.start_angle_deg);
    const float end_rad   = glm::radians(p_.end_angle_deg);
    const float angle     = start_rad + (end_rad - start_rad) * t;

    cam.position = {
        p_.target.x + p_.radius * std::sin(angle),
        p_.target.y + p_.height,
        p_.target.z + p_.z_offset + p_.radius * (std::cos(angle) - 1.0f)
    };
    cam.rotation = { p_.tilt_deg, -glm::degrees(angle), 0.0f };
    cam.zoom = p_.zoom;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

// ── ParallaxPanMotion ──────────────────────────────────────────────────────
Camera2_5D ParallaxPanMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float t = easing_apply(p_.easing,
                                 parametric_t(ctx, static_cast<i32>(p_.duration)));
    cam.position = {
        p_.from_position.x + (p_.to_position.x - p_.from_position.x) * t,
        p_.from_position.y + (p_.to_position.y - p_.from_position.y) * t,
        p_.from_position.z + (p_.to_position.z - p_.from_position.z) * t,
    };
    cam.zoom = p_.zoom;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

// ── DollyZoomMotion ────────────────────────────────────────────────────────
Camera2_5D DollyZoomMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float t = easing_apply(p_.easing,
                                 parametric_t(ctx, static_cast<i32>(p_.duration)));
    cam.position = {
        p_.from_position.x + (p_.to_position.x - p_.from_position.x) * t,
        p_.from_position.y + (p_.to_position.y - p_.from_position.y) * t,
        p_.from_position.z + (p_.to_position.z - p_.from_position.z) * t,
    };
    cam.zoom = p_.from_zoom + (p_.to_zoom - p_.from_zoom) * t;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

// ── FocusPullMotion ────────────────────────────────────────────────────────
Camera2_5D FocusPullMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float t = easing_apply(p_.easing,
                                 parametric_t(ctx, static_cast<i32>(p_.duration)));
    cam.position = p_.position;
    cam.zoom = p_.zoom;
    cam.point_of_interest_enabled = false;
    // Focus-pull animates dof.focus_z between from_focus_z and to_focus_z.
    cam.dof.focus_z =
        p_.from_focus_z + (p_.to_focus_z - p_.from_focus_z) * t;
    cam.dof.aperture = p_.aperture;
    cam.dof.max_blur = p_.max_blur;
    cam.dof.enabled = true;
    return cam;
}

// ── LowAngleRevealMotion ──────────────────────────────────────────────────
Camera2_5D LowAngleRevealMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float t = easing_apply(p_.easing,
                                 parametric_t(ctx, static_cast<i32>(p_.duration)));
    cam.position = {
        p_.from_position.x + (p_.to_position.x - p_.from_position.x) * t,
        p_.from_position.y + (p_.to_position.y - p_.from_position.y) * t,
        p_.from_position.z + (p_.to_position.z - p_.from_position.z) * t,
    };
    cam.rotation = {
        p_.from_tilt + (p_.to_tilt - p_.from_tilt) * t,
        0.0f, 0.0f
    };
    cam.zoom = p_.zoom;
    cam.point_of_interest = p_.target;
    cam.point_of_interest_enabled = true;
    return cam;
}

// ── SubtleFloatMotion ─────────────────────────────────────────────────────
Camera2_5D SubtleFloatMotion::evaluate(const CameraMotionContext& ctx) const {
    Camera2_5D cam;
    cam.enabled = true;
    const float frames_into = static_cast<float>(ctx.sample_time.frame);
    cam.position = {
        p_.base_position.x + p_.x_amplitude *
            std::sin(frames_into * p_.x_frequency),
        p_.base_position.y + p_.y_amplitude *
            std::cos(frames_into * p_.y_frequency),
        p_.base_position.z + p_.z_amplitude *
            std::sin(frames_into * p_.z_frequency + 1.0f),
    };
    cam.zoom = p_.zoom;
    cam.point_of_interest_enabled = false;
    return cam;
}

// ── register_camera_rig_motions() — entry point ────────────────────────────
void register_camera_rig_motions() {
    auto& reg = CameraMotionRegistry::instance();
    if (reg.is_frozen()) return;

    // Register default-config instances so external V1 lookup by id works.
    using namespace chronon3d::camera_rig;
    if (!reg.has("camera.rig.hero_push_in"))
        reg.register_motion(std::make_shared<HeroPushInMotion>(HeroPushInParams{}));
    if (!reg.has("camera.rig.orbit_yaw"))
        reg.register_motion(std::make_shared<OrbitYawMotion>(OrbitYawParams{}));
    if (!reg.has("camera.rig.parallax_pan"))
        reg.register_motion(std::make_shared<ParallaxPanMotion>(ParallaxPanParams{}));
    if (!reg.has("camera.rig.dolly_zoom"))
        reg.register_motion(std::make_shared<DollyZoomMotion>(DollyZoomParams{}));
    if (!reg.has("camera.rig.focus_pull"))
        reg.register_motion(std::make_shared<FocusPullMotion>(FocusPullParams{}));
    if (!reg.has("camera.rig.low_angle_reveal"))
        reg.register_motion(std::make_shared<LowAngleRevealMotion>(LowAngleRevealParams{}));
    if (!reg.has("camera.rig.subtle_float"))
        reg.register_motion(std::make_shared<SubtleFloatMotion>(SubtleFloatParams{}));
}

} // namespace chronon3d::camera_v1
