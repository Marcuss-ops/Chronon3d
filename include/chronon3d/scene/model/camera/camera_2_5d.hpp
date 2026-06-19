#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <string>
#include <memory_resource>
#include <glm/gtx/quaternion.hpp>  // glm::quatLookAtLH stable path

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

// ── Camera2_5DProjectionMode and CameraOpticsMode are now defined in ────────
// camera_projection_source.hpp so that the projection pipeline can depend on
// the interface without pulling in the full camera_2_5d.hpp header.

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

    // Projection mode: Zoom (default) or Fov.  Legacy selector kept for
    // backward compatibility (composers that still want Camera2_5D level
    // control over the projection).  See CameraOpticsMode on the rig for
    // the canonical optics contract used by the projection pipeline.
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    // Optics mode — the canonical contract used by CameraRig and the
    // projection pipeline.  Decoupled from DoF: a physical lens can be
    // active (PhysicalLens) while DoF.enabled is false.
    CameraOpticsMode optics_mode{CameraOpticsMode::Zoom};

    // Perspective strength. At depth == zoom, perspective_scale == 1.
    // Used when projection_mode == Zoom or optics_mode == Zoom.
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

    // ── Canonical orientation helpers ─────────────────────────────────────
    //
    // resolve_look_at_orientation() returns the canonical orientation
    // quaternion for this camera.  The matrix is built as the product of
    // distinct factors in a fixed order so the result is consistent across
    // rig presets, animation, and rigging code:
    //
    //   TwoNode:
    //     q = qLookAt * qLocal * qX(tilt) * qY(pan) * qZ(roll)
    //   OneNode:
    //     q = qLocal * qX(tilt) * qY(pan) * qZ(roll)
    //
    // The TwoNode c2w rotation is constructed directly from an explicit
    // orthonormal basis (right, up, fwd_w) with a deterministic 2nd-axis
    // swap when fwd_w is near-axial to world-Y (to break the parallel-
    // vector degeneracy in the cross product).  This avoids the
    // glm::lookAtLH → inverse → quat_cast chain which can produce non-
    // unit quaternions when the lookAt matrix has det = -1 for certain
    // camera/target/up alignments (e.g. fwd_w ≈ (0,±1,0) or ≈ (0,0,1)
    // with the standard worldUp = (0,1,0)).
    [[nodiscard]] Quat resolve_look_at_orientation() const {
        // Euler rotations in degrees applied in the order X → Y → Z.
        // Quaternions multiply right-to-left, so the right-most factor is
        // applied first to a column vector.
        const f32 rx = glm::radians(rotation.x);  // tilt  (X)
        const f32 ry = glm::radians(rotation.y);  // pan   (Y)
        const f32 rz = glm::radians(rotation.z);  // roll  (Z)
        const Quat qX = glm::angleAxis(rx, Vec3{1.0f, 0.0f, 0.0f});
        const Quat qY = glm::angleAxis(ry, Vec3{0.0f, 1.0f, 0.0f});
        const Quat qZ = glm::angleAxis(rz, Vec3{0.0f, 0.0f, 1.0f});
        // qLocal = identity rotor. Constructed via angleAxis(0) instead
        // of Quat{} so the result is guaranteed unit-identity across GLM
        // 0.9.9 and 1.x regardless of GLM_CONFIG_DEFAULTED_DEFAULT_CTOR.
        const Quat qLocal = glm::angleAxis(0.0f, Vec3{1.0f, 0.0f, 0.0f});

        if (point_of_interest_enabled &&
            glm::length(point_of_interest - position) > 0.001f) {
            // TwoNode: build the c2w basis explicitly. This is bit-exact,
            // det = +1 by construction, and produces a valid rotation
            // matrix whose quat_cast result is a unit quat for any
            // non-degenerate fwd_w (including axial targets like (0,0,1)
            // where the matrix collapses to identity).
            //
            // Cross-product order (`right = cross(refUp, fwd_w)`,
            // `up = cross(fwd_w, right)`) is the canonical RH-style
            // c2w construction: for fwd_w = (0,0,1) and refUp = (0,1,0),
            // right = (1,0,0) and up = (0,1,0) — identity basis —
            // det = +1, quat_cast = identity quat, qnorm = 1 bit-exact.
            const Vec3 fwd_w = glm::normalize(point_of_interest - position);
            const Vec3 refUp = select_ref_up(fwd_w);
            const Vec3 right = glm::normalize(glm::cross(refUp, fwd_w));
            const Vec3 up    = glm::normalize(glm::cross(fwd_w, right));
            // Explicit column assignment (vs glm::mat3(right,up,fwd_w)
            // constructor) — more readable and version-independent.
            glm::mat3 c2w_basis;
            c2w_basis[0] = right;
            c2w_basis[1] = up;
            c2w_basis[2] = fwd_w;
            const Quat qLookAt = glm::quat_cast(c2w_basis);
            // Compose: qLookAt * qLocal * qX(tilt) * qY(pan) * qZ(roll).
            return qLookAt * qLocal * qX * qY * qZ;
        }

        // OneNode: orientation * X * Y * Z (matches the legacy Euler
        // composition used by Camera2_5D::rotation_quaternion()).
        return qLocal * qX * qY * qZ;
    }

    // ── 2nd-axis swap helper (canonical lookAt pattern) ───────────────────
    //
    // Returns the world-frame "reference up" vector used to break the
    // forward-axle-of-lookAt degeneracy.  When forward is near-axial to the
    // primary up axis (world-Y), the cross product refUp × fwd_w → 0 and
    // the canonical lookAt chain collapses.  The canonical fix is to pick
    // a different perpendicular reference axis — world-Z if forward is
    // near Y, else world-X as a last resort.  This freezes the spec
    // invariant in code (independent of GLM's internal handling which
    // differs across GLM 0.9.9 and 1.0.x).
    [[nodiscard]] static Vec3 select_ref_up(const Vec3& fwd_w) {
        // Threshold widened to ~5.7° so smooth target slides across y=0 / z=0
        // stay on world-Y (no discontinuous refUp flip on a single-step
        // crossing past the boundary).
        constexpr f32 kAxialEps = 1e-2f;
        if (std::abs(fwd_w.y) < 1.0f - kAxialEps) {
            return Vec3{0.0f, 1.0f, 0.0f};   // world Y — primary refUp
        }
        if (std::abs(fwd_w.z) < 1.0f - kAxialEps) {
            return Vec3{0.0f, 0.0f, 1.0f};   // world Z — secondary refUp
        }
        return Vec3{1.0f, 0.0f, 0.0f};       // world X — last-resort refUp
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
        // Canonical view matrix — numerically stable.
        //
        //   TwoNode: glm::lookAtLH produces the view matrix; the local
        //            X/Y/Z rotations are composed as a world-space
        //            pre-multiplication (rotation applied first, then
        //            look-at) so the spec order qLookAt * qX * qY * qZ
        //            matches the matrix form.
        //   OneNode: rotation drives the view directly.
        if (point_of_interest_enabled &&
            glm::length(point_of_interest - position) > 0.001f) {
            Mat4 view = glm::lookAtLH(position, point_of_interest,
                                       Vec3{0.0f, 1.0f, 0.0f});
            // Apply local X/Y/Z rotations as a world-space post-multiplication
            // (world → rotate → look-at) using the canonical quaternion
            // composition qX * qY * qZ.  Identity quaternion gives identity
            // matrix and is a no-op when rotation is exactly zero.
            const f32 rx = glm::radians(rotation.x);
            const f32 ry = glm::radians(rotation.y);
            const f32 rz = glm::radians(rotation.z);
            const Quat qLocal = glm::angleAxis(rx, Vec3{1.0f, 0.0f, 0.0f}) *
                                glm::angleAxis(ry, Vec3{0.0f, 1.0f, 0.0f}) *
                                glm::angleAxis(rz, Vec3{0.0f, 0.0f, 1.0f});
            view = view * glm::toMat4(qLocal);
            return view;
        }
        return math::camera_view_matrix(position, rotation_quaternion());
    }
};

using Camera2_5DRuntime = Camera2_5D;

} // namespace chronon3d
