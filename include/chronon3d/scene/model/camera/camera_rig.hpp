#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <string>
#include <utility>
#include <functional>

namespace chronon3d {
class SceneBuilder;

enum class CameraRigMode {
    OneNode,
    TwoNode
};

struct CameraRigDOF {
    bool enabled{false};
    std::string focus_target_name;

    // Legacy model.
    AnimatedValue<f32> focus_z{0.0f};
    AnimatedValue<f32> aperture{0.015f};
    AnimatedValue<f32> max_blur{24.0f};
    bool use_target_z{false};

    // Physical lens model.
    AnimatedValue<f32> focal_length{50.0f};
    AnimatedValue<f32> sensor_width{36.0f};
    AnimatedValue<f32> sensor_height{24.0f};
    AnimatedValue<f32> f_stop{2.8f};
    AnimatedValue<f32> focus_distance{1000.0f};
    AnimatedValue<f32> close_focus{450.0f};
    GateFit             gate_fit{GateFit::Fill};
    bool use_physical_model{false};
};

struct CameraRigMotionBlur {
    bool enabled{false};
    int samples{8};
    f32 shutter_angle_deg{180.0f};
    f32 shutter_phase_deg{-90.0f};
    TemporalSamplePattern pattern{TemporalSamplePattern::Stratified};
    TemporalFilter        filter{TemporalFilter::Box};
    u64  jitter_seed{0x3A5C9F1E};
};

struct CameraRig {
    std::string name{"MainCameraRig"};
    CameraRigMode mode{CameraRigMode::TwoNode};
    std::string parent_name;
    std::string target_name;

    AnimatedValue<Vec3> target{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<f32> orbit_yaw{0.0f};
    AnimatedValue<f32> orbit_pitch{0.0f};
    AnimatedValue<f32> orbit_radius{1000.0f};

    AnimatedValue<Vec3> track{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<f32> dolly{0.0f};

    AnimatedValue<f32> pan{0.0f};
    AnimatedValue<f32> tilt{0.0f};
    AnimatedValue<f32> roll{0.0f};

    AnimatedValue<f32> zoom{1000.0f};
    AnimatedValue<f32> fov_deg{50.0f};
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    CameraRigDOF dof{};
    CameraRigMotionBlur motion_blur{};

    [[nodiscard]] Camera2_5D evaluate(
        Frame frame,
        const TransformResolverResult* resolved = nullptr
    ) const {
        return evaluate(SampleTime::from_frame_int(frame, FrameRate{30, 1}), resolved);
    }

    /// Sub-frame evaluation — enables true multi-sample motion blur.
    [[nodiscard]] Camera2_5D evaluate(
        SampleTime time,
        const TransformResolverResult* resolved = nullptr
    ) const;
};

} // namespace chronon3d

// ── Keep old camera_rig namespace and animated presets for backward compatibility ──
//
// Includes are at FILE SCOPE (outside any namespace) to prevent
// C++17 nested-namespace-definition (namespace A::B) from creating
// an extra nesting level when included inside namespace chronon3d.
#include <chronon3d/scene/camera/camera_rig_params.hpp>

namespace chronon3d {
namespace camera_rig {

enum class RigMode {
    OneNode,
    TwoNode
};

struct CameraRig {
    RigMode mode{RigMode::TwoNode};
    bool enabled{true};

    std::string controller_name{"camera_rig"};
    std::string target_name{"camera_target"};

    AnimatedValue<Vec3> controller_position{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> controller_rotation{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> controller_anchor{Vec3{0.0f, 0.0f, 0.0f}};

    AnimatedValue<Vec3> target_position{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> target_rotation{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> target_anchor{Vec3{0.0f, 0.0f, 0.0f}};

    AnimatedValue<Vec3> camera_position{Vec3{0.0f, 0.0f, -1000.0f}};
    AnimatedValue<Vec3> camera_rotation{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<f32> zoom{1000.0f};
    AnimatedValue<f32> fov_deg{50.0f};

    DepthOfFieldSettings dof{};
    bool use_fov{false};

    [[nodiscard]] Camera2_5D bake(Frame frame, std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        Camera2_5D cam;
        cam.enabled = enabled;
        cam.position = camera_position.evaluate(frame);
        cam.rotation = camera_rotation.evaluate(frame);
        cam.zoom = zoom.evaluate(frame);
        cam.fov_deg = fov_deg.evaluate(frame);
        cam.projection_mode = use_fov ? Camera2_5DProjectionMode::Fov : Camera2_5DProjectionMode::Zoom;
        cam.dof = dof;
        cam.parent_name = std::pmr::string{controller_name, res};
        cam.point_of_interest_enabled = (mode == RigMode::TwoNode);
        if (mode == RigMode::TwoNode) {
            cam.target_name = std::pmr::string{target_name, res};
        }
        return cam;
    }

    void apply(SceneBuilder& s, Frame frame, std::function<void(SceneBuilder&)> add_target_content) const;
    void apply(SceneBuilder& s, Frame frame) const;
};

} // namespace camera_rig

} // namespace chronon3d

namespace chronon3d::camera_rig {

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

inline AnimatedCamera2_5D orbit_yaw(const OrbitYawParams& p = {}) {
    AnimatedCamera2_5D cam;
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
    cam.point_of_interest.set(p.target);
    cam.point_of_interest_enabled = true;
    return cam;
}

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
