#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <string>
#include <memory_resource>

namespace chronon3d {

// ── Temporal sample pattern for motion blur ──────────────────────────────
// Controls how sub-frame samples are distributed across the shutter window.
// All patterns produce deterministic results for the same seed.
enum class TemporalSamplePattern {
    Uniform,     // evenly spaced samples (no jitter)
    Stratified,  // one random point per equal-sized stratum
    Halton       // low-discrepancy Halton sequence (2,3)
};

// ── Temporal reconstruction filter ───────────────────────────────────────
// Weight function applied across the shutter window.
enum class TemporalFilter {
    Box,         // equal weight for all samples (1/N)
    Triangle,    // linear ramp: highest weight at center, zero at edges
    Gaussian     // Gaussian bell curve (sigma = exposure_duration / 4)
};

struct MotionBlurSettings {
    bool enabled{false};
    int  samples{8};                    // number of subframes to accumulate
    f32  shutter_angle_deg{180.0f};     // degrees; 180 = half-frame exposure
    f32  shutter_phase_deg{-90.0f};     // degrees; -90 centres exposure around the frame

    TemporalSamplePattern pattern{TemporalSamplePattern::Stratified};
    TemporalFilter        filter{TemporalFilter::Box};

    // Deterministic seed for jitter patterns. Same seed → same jitter.
    // Defaults to 0x3A5C9F1E (arbitrary constant).
    u64  jitter_seed{0x3A5C9F1E};
};

struct DepthOfFieldSettings {
    bool  enabled{false};

    // Legacy simple model (backward compatible):
    //   blur = |layer_z - focus_z| * aperture, clamped to max_blur.
    f32   focus_z{0.0f};      // world-space Z at which blur = 0
    f32   aperture{0.015f};   // blur per unit of Z distance from focus
    f32   max_blur{24.0f};    // clamp: pixels of blur at extreme depths

    // Physical lens model: camera-space distance to focus plane (scene units).
    // Used by compute_physical_coc() when use_physical_model is true.
    // Evaluated per-frame from camera_rig / animated_camera.
    f32   focus_distance{1000.0f};
    bool  use_physical_model{false};

    // Near / far bokeh separation (for future iris shape rendering).
    // Set to 0 to disable per-side bokeh control (uses uniform blur radius).
    f32   near_bokeh_radius{0.0f};  // max blur radius for objects closer than focus
    f32   far_bokeh_radius{0.0f};   // max blur radius for objects farther than focus
};

// How perspective strength is specified for Camera2_5D.
// Zoom: use camera.zoom directly (focal_length = zoom).
// Fov:  derive focal length from camera.fov_deg and viewport height.
enum class Camera2_5DProjectionMode {
    Zoom, // default — focal length == camera.zoom
    Fov   // derive focal from fov_deg (true FOV-based projection)
};

struct Camera2_5D {
    bool enabled{false};

    // Set to true by AnimatedCamera2_5D::evaluate() and by camera motion
    // appliers/presets that mutate the camera per-frame.  Read by
    // SceneHasher::camera_is_static() to decide whether the camera is
    // effectively time-dependent for fast-path / caching purposes.
    // Defaults to false (static camera).
    bool is_animated{false};

    // Camera position in screen-space world coordinates.
    // Default: 1000 units "in front" of the z=0 plane.
    Vec3 position{0.0f, 0.0f, -1000.0f};

    // Stored for future two-node camera support; not used for orbit in v1.
    Vec3 point_of_interest{0.0f, 0.0f, 0.0f};
    bool point_of_interest_enabled{false};
    bool hierarchy_baked{false};

    // Projection mode: Zoom (default) or Fov.
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    // Perspective strength. At depth == zoom, perspective_scale == 1.
    // Used when projection_mode == Zoom.
    f32 zoom{1000.0f};

    // Field of view in degrees. Used when projection_mode == Fov.
    // 35° ≈ telephoto (less distortion), 70° ≈ wide angle (more parallax).
    f32 fov_deg{50.0f};

    // Hierarchy
    std::pmr::string parent_name;
    std::pmr::string target_name; // If set, POI is resolved from this layer's world position
    Vec3 rotation{0, 0, 0};

    // Physical lens model — carries focal length, sensor size, f-stop,
    // close focus, and gate-fit mode.  Replaces the loose lens parameters
    // that were previously scattered in DepthOfFieldSettings.
    //
    // The lens model is independent of DoF: you can set a lens without
    // enabling depth of field.  DoF reads its physical parameters from
    // here when dof.use_physical_model is true.
    LensModel lens{50.0f, 2.8f, 450.0f, 36.0f, 24.0f, GateFit::Fill};

    DepthOfFieldSettings dof;

    // Motion blur settings — populated by CameraRig::evaluate() and
    // AnimatedCamera2_5D flows.  The render pipeline prefers camera-level
    // motion blur over the global RenderSettings value when the camera
    // has explicit motion blur configuration.
    MotionBlurSettings motion_blur{};

    [[nodiscard]] Quat rotation_quaternion() const {
        return math::camera_rotation_quat(rotation);
    }

    [[nodiscard]] Vec3 rotation_euler() const {
        return rotation;
    }

    void set_rotation_euler(Vec3 euler_deg) {
        rotation = euler_deg;
    }

    void set_tilt(f32 degrees) {
        rotation.x = degrees;
    }

    void add_tilt(f32 delta_degrees) {
        rotation.x += delta_degrees;
    }

    void set_pan(f32 degrees) {
        rotation.y = degrees;
    }

    void add_pan(f32 delta_degrees) {
        rotation.y += delta_degrees;
    }

    void set_roll(f32 degrees) {
        rotation.z = degrees;
    }

    void add_roll(f32 delta_degrees) {
        rotation.z += delta_degrees;
    }

    [[nodiscard]] Mat4 view_matrix() const {
        if (point_of_interest_enabled && glm::length(point_of_interest - position) > 0.001f) {
            Mat4 view = glm::lookAtLH(position, point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
            if (std::abs(rotation.z) > 0.0001f) {
                const f32 r = glm::radians(rotation.z);
                Mat4 roll_m = glm::rotate(Mat4{1.0f}, r, Vec3{0.0f, 0.0f, 1.0f});
                view = roll_m * view;
            }
            return view;
        }
        const Quat rot = rotation_quaternion();
        return math::camera_view_matrix(position, rot);
    }
};

using Camera2_5DRuntime = Camera2_5D;

} // namespace chronon3d
