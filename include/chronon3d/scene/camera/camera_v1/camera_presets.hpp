#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1_presets.hpp
//
// Inline convenience presets that return canonical V1 pose sources.
// Canonical V1 camera authoring presets.
// pose sources instead of maintaining an imperative camera type.
//
// These presets are feature-zone code — they depend on the full Camera2_5D and
// camera descriptor types.
// drag in.
// ==============================================================================

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_preset_params.hpp>

namespace chronon3d::camera_v1::presets {

inline camera_v1::PoseTracksSource hero_push_in(const HeroPushInParams& p = {}) {
    camera_v1::PoseTracksSource cam;
    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);
    cam.rotation
        .key(p.start_frame, Vec3{p.from_tilt, p.from_yaw, 0.0f})
        .key(p.start_frame + p.duration, Vec3{p.to_tilt, p.to_yaw, 0.0f}, p.easing);
    cam.zoom.set(p.zoom);
    cam.use_target = false;
    return cam;
}

inline camera_v1::PoseTracksSource orbit_yaw(const OrbitYawParams& p = {}) {
    camera_v1::PoseTracksSource cam;
    const f32 start_rad = glm::radians(p.start_angle_deg);
    const f32 end_rad   = glm::radians(p.end_angle_deg);
    const Frame end_frame = p.start_frame + p.duration;

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
    cam.target.set(p.target);
    cam.use_target = true;
    return cam;
}

inline camera_v1::PoseTracksSource parallax_pan(const ParallaxPanParams& p = {}) {
    camera_v1::PoseTracksSource cam;
    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);
    cam.zoom.set(p.zoom);
    cam.target.set(p.target);
    cam.use_target = true;
    return cam;
}

inline camera_v1::PoseTracksSource dolly_zoom(const DollyZoomParams& p = {}) {
    camera_v1::PoseTracksSource cam;
    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);
    cam.zoom
        .key(p.start_frame, p.from_zoom)
        .key(p.start_frame + p.duration, p.to_zoom, p.easing);
    cam.target.set(p.target);
    cam.use_target = true;
    return cam;
}

inline camera_v1::PoseTracksSource focus_pull(const FocusPullParams& p = {}) {
    camera_v1::PoseTracksSource cam;
    cam.position.set(p.position);
    cam.zoom.set(p.zoom);
    cam.focus_distance
        .key(p.start_frame, p.from_focus_z)
        .key(p.start_frame + p.duration, p.to_focus_z, p.easing);
    cam.aperture.set(p.aperture);
    cam.max_blur.set(p.max_blur);
    cam.use_target = false;
    return cam;
}

inline camera_v1::PoseTracksSource low_angle_reveal(const LowAngleRevealParams& p = {}) {
    camera_v1::PoseTracksSource cam;
    cam.position
        .key(p.start_frame, p.from_position)
        .key(p.start_frame + p.duration, p.to_position, p.easing);
    cam.rotation
        .key(p.start_frame, Vec3{p.from_tilt, 0.0f, 0.0f})
        .key(p.start_frame + p.duration, Vec3{p.to_tilt, 0.0f, 0.0f}, p.easing);
    cam.zoom.set(p.zoom);
    cam.target.set(p.target);
    cam.use_target = true;
    return cam;
}

inline camera_v1::PoseTracksSource subtle_float(const SubtleFloatParams& p = {}) {
    camera_v1::PoseTracksSource cam;
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
    cam.use_target = false;
    return cam;
}

} // namespace chronon3d::camera_v1::presets
