#pragma once

#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/animation/easing.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>

namespace chronon3d::camera_rig {

// ═══════════════════════════════════════════════════════════════════════════════
// CameraRig — high-level cinematic camera motion presets.
//
// Each function returns a fully-configured AnimatedCamera2_5D with
// professionally-tuned keyframes, easing, and timing.
//
// Usage:
//   auto cam = camera_rig::hero_push_in();
//   for (Frame f = 0; f < 90; ++f) {
//       scene.set_camera_2_5d(cam.evaluate(f));
//   }
// ═══════════════════════════════════════════════════════════════════════════════

// ── Parameter structs ─────────────────────────────────────────────────────────

struct HeroPushInParams {
    Vec3  from_position{0.0f, 0.0f, -1200.0f};
    Vec3  to_position{0.0f, 0.0f, -750.0f};
    f32   from_tilt{-4.0f};
    f32   to_tilt{2.0f};
    f32   from_yaw{0.0f};
    f32   to_yaw{3.0f};
    f32   zoom{1000.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{EasingCurve::cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f)};
};

struct OrbitYawParams {
    Vec3  target{0.0f, 0.0f, 0.0f};
    f32   radius{300.0f};
    f32   height{40.0f};
    f32   z_offset{-1000.0f};
    f32   start_angle_deg{-25.0f};
    f32   end_angle_deg{25.0f};
    f32   tilt_deg{-5.0f};
    f32   zoom{1000.0f};
    Frame duration{120};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutCubic};
};

struct ParallaxPanParams {
    Vec3  from_position{-180.0f, 0.0f, -1000.0f};
    Vec3  to_position{180.0f, 0.0f, -1000.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    f32   zoom{1000.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutSine};
};

struct DollyZoomParams {
    Vec3  from_position{0.0f, 0.0f, -1200.0f};
    Vec3  to_position{0.0f, 0.0f, -600.0f};
    f32   from_zoom{1200.0f};
    f32   to_zoom{600.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutCubic};
};

struct FocusPullParams {
    Vec3  position{0.0f, 0.0f, -1000.0f};
    f32   zoom{1000.0f};
    f32   from_focus_z{0.0f};
    f32   to_focus_z{500.0f};
    f32   aperture{0.03f};
    f32   max_blur{24.0f};
    Frame duration{60};
    Frame start_frame{0};
    EasingCurve easing{Easing::InOutCubic};
};

struct LowAngleRevealParams {
    Vec3  from_position{0.0f, -180.0f, -1100.0f};
    Vec3  to_position{0.0f, 40.0f, -800.0f};
    f32   from_tilt{25.0f};
    f32   to_tilt{0.0f};
    Vec3  target{0.0f, 0.0f, 0.0f};
    f32   zoom{1000.0f};
    Frame duration{90};
    Frame start_frame{0};
    EasingCurve easing{EasingCurve::cubic_bezier(0.33f, 1.0f, 0.68f, 1.0f)};
};

struct SubtleFloatParams {
    Vec3  base_position{0.0f, 0.0f, -1000.0f};
    f32   x_amplitude{15.0f};
    f32   y_amplitude{8.0f};
    f32   z_amplitude{20.0f};
    f32   x_frequency{0.3f};
    f32   y_frequency{0.2f};
    f32   z_frequency{0.15f};
    f32   zoom{1000.0f};
    Frame duration{300};
    Frame start_frame{0};
};

// ── Preset functions ──────────────────────────────────────────────────────────

/// Dramatic push-in: camera dollies forward with slight tilt and yaw.
/// Classic cinematic hero shot — the camera "enters" the space.
inline AnimatedCamera2_5D hero_push_in(const HeroPushInParams& p = {}) {
    AnimatedCamera2_5D cam;

    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);

    cam.rotation
        .key(p.start_frame, Vec3{p.from_tilt, p.from_yaw, 0.0f})
        .key(p.start_frame + p.duration, Vec3{p.to_tilt, p.to_yaw, 0.0f}, p.easing);

    cam.zoom.set(p.zoom);
    cam.point_of_interest_enabled = false;
    return cam;
}

/// Yaw orbit: camera sweeps around a target on the Y axis.
/// Creates a 3D reveal effect with parallax.
inline AnimatedCamera2_5D orbit_yaw(const OrbitYawParams& p = {}) {
    AnimatedCamera2_5D cam;

    const f32 start_rad = glm::radians(p.start_angle_deg);
    const f32 end_rad   = glm::radians(p.end_angle_deg);
    const Frame end_frame = p.start_frame + p.duration;

    // Key the orbit by computing position from angle at several keyframes
    constexpr int kSamples = 5;
    for (int i = 0; i <= kSamples; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(kSamples);
        const f32 angle = start_rad + (end_rad - start_rad) * p.easing.apply(t);
        const Frame f = p.start_frame + Frame{static_cast<i32>(std::round(t * static_cast<f32>(p.duration)))};

        const Vec3 pos{
            p.target.x + p.radius * std::sin(angle),
            p.target.y + p.height,
            p.target.z + p.z_offset + p.radius * (std::cos(angle) - 1.0f)
        };
        cam.position.key(f, pos);
        cam.rotation.key(f, Vec3{p.tilt_deg, -glm::degrees(angle), 0.0f});
    }

    cam.position.key(end_frame, Vec3{
        p.target.x + p.radius * std::sin(end_rad),
        p.target.y + p.height,
        p.target.z + p.z_offset + p.radius * (std::cos(end_rad) - 1.0f)
    });

    cam.zoom.set(p.zoom);
    cam.point_of_interest.set(p.target);
    cam.point_of_interest_enabled = true;
    return cam;
}

/// Parallax pan: horizontal tracking shot with smooth ease.
/// Foreground elements move faster than background — classic parallax.
inline AnimatedCamera2_5D parallax_pan(const ParallaxPanParams& p = {}) {
    AnimatedCamera2_5D cam;

    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);

    cam.zoom.set(p.zoom);
    cam.point_of_interest.set(p.target);
    cam.point_of_interest_enabled = true;
    return cam;
}

/// Dolly zoom (Vertigo effect): camera moves in while zooming out.
/// Subject stays the same size while background perspective changes dramatically.
inline AnimatedCamera2_5D dolly_zoom(const DollyZoomParams& p = {}) {
    AnimatedCamera2_5D cam;

    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);

    cam.zoom
        .key(p.start_frame, p.from_zoom)
        .key(p.start_frame + p.duration, p.to_zoom, p.easing);

    cam.point_of_interest.set(p.target);
    cam.point_of_interest_enabled = true;
    return cam;
}

/// Focus pull (rack focus): shift depth of field from one z-plane to another.
/// Blurs foreground/background transition for cinematic storytelling.
inline AnimatedCamera2_5D focus_pull(const FocusPullParams& p = {}) {
    AnimatedCamera2_5D cam;

    cam.position.set(p.position);
    cam.zoom.set(p.zoom);

    cam.focus_z
        .key(p.start_frame, p.from_focus_z)
        .key(p.start_frame + p.duration, p.to_focus_z, p.easing);

    cam.aperture.set(p.aperture);
    cam.max_blur.set(p.max_blur);
    cam.point_of_interest_enabled = false;
    return cam;
}

/// Low-angle reveal: camera starts low and rises to eye level with upward tilt.
/// Creates a dramatic "hero entrance" look.
inline AnimatedCamera2_5D low_angle_reveal(const LowAngleRevealParams& p = {}) {
    AnimatedCamera2_5D cam;

    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);

    cam.rotation
        .key(p.start_frame, Vec3{p.from_tilt, 0.0f, 0.0f})
        .key(p.start_frame + p.duration, Vec3{p.to_tilt, 0.0f, 0.0f}, p.easing);

    cam.zoom.set(p.zoom);
    cam.point_of_interest.set(p.target);
    cam.point_of_interest_enabled = true;
    return cam;
}

/// Subtle float: a gentle idling camera motion using sinusoidal oscillation.
/// Creates a living, breathing handheld feel over a long duration.
inline AnimatedCamera2_5D subtle_float(const SubtleFloatParams& p = {}) {
    AnimatedCamera2_5D cam;

    const Frame end_frame = p.start_frame + p.duration;
    constexpr int kSamples = 12;
    const f32 frames_per_sample = static_cast<f32>(p.duration) / static_cast<f32>(kSamples);

    for (int i = 0; i <= kSamples; ++i) {
        const f32 phase = static_cast<f32>(i) * frames_per_sample;
        const Frame f = p.start_frame + Frame{static_cast<i32>(std::round(phase))};

        const Vec3 pos{
            p.base_position.x + p.x_amplitude * std::sin(phase * p.x_frequency),
            p.base_position.y + p.y_amplitude * std::cos(phase * p.y_frequency),
            p.base_position.z + p.z_amplitude * std::sin(phase * p.z_frequency + 1.0f)
        };
        cam.position.key(f, pos);
    }

    cam.zoom.set(p.zoom);
    cam.point_of_interest_enabled = false;
    return cam;
}

} // namespace chronon3d::camera_rig
