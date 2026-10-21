#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>
#include <memory_resource>

namespace chronon3d {

// TemporalSamplePattern and TemporalFilter are now in camera_common_types.hpp

// ── Motion blur mode ──────────────────────────────────────────────────────────
//
// PR1 — mutually exclusive motion-blur modes, replacing the previous
// `bool enabled` implicit flag.  The compositor reads this field to decide
// which blur pipeline to activate:
//
//   Off                   — no motion blur (default fast path)
//   TemporalAccumulation  — render N sub-frames and accumulate framebuffers
//                            (acceleration costs N× renders; produces correct
//                            blur from both camera motion AND layer animation)
//   VelocityApproximation — single-frame render; velocity derived from
//                            previous-vs-current frame block-matching produces
//                            the blur (cheaper; loses intra-frame animation)
//
// TemporalAccumulation and VelocityApproximation are mutually exclusive:
// enabling both at once is a configuration error and the renderer reports
// it as a pre-flight failure.
enum class MotionBlurMode {
    Off = 0,
    TemporalAccumulation = 1,
    VelocityApproximation = 2,
};

struct MotionBlurSettings {
    /// PR1 / TICKET-026 — sole source of truth for "is motion blur active".
    /// The legacy `bool enabled` field has been deleted; everything that
    /// used to gate on `settings.motion_blur.enabled` now uses the helper
    /// `is_motion_blur_active(settings.motion_blur)` (defined below).
    /// Either `TemporalAccumulation` (sub-frame accumulator) or
    /// `VelocityApproximation` (single-frame velocity buffer) is
    /// "motion-blur on"; `Off` is "no motion blur".
    MotionBlurMode mode{MotionBlurMode::Off};
    int  samples{8};                    // number of subframes to accumulate
    f32  shutter_angle_deg{180.0f};     // degrees; 180 = half-frame exposure
    f32  shutter_phase_deg{-90.0f};     // degrees; -90 centres exposure around the frame

    TemporalSamplePattern pattern{TemporalSamplePattern::Stratified};
    TemporalFilter        filter{TemporalFilter::Box};

    // Deterministic seed for jitter patterns. Same seed → same jitter.
    // Defaults to 0x3A5C9F1E (arbitrary constant).
    u64  jitter_seed{0x3A5C9F1E};
};

// TICKET-026 — free helper, the canonical replacement for the deleted
// `MotionBlurSettings::enabled` bool.  Any code that needs to ask "is
// motion blur active for this settings struct" should call this.  Kept as
// a free inline helper so it can be called from headers AND TUs without
// pulling in extra linkage, and so future enum additions
// (e.g. `MotionBlurMode::DisabledButRenderAnyway`) stay centralised.
[[nodiscard]] inline bool is_motion_blur_active(const MotionBlurSettings& s) noexcept {
    return s.mode != MotionBlurMode::Off;
}

// TICKET-026 NIT — bound enum widening.  Guard every u8(u8 mix used by
// the descriptor fingerprint against future drift.
static_assert(static_cast<std::uint32_t>(MotionBlurMode::VelocityApproximation) < 256,
              "MotionBlurMode fingerprint mixes u8 — keep enum < 256 values");

// DepthOfFieldSettings is now in camera_common_types.hpp

// ── Camera2_5DProjectionMode and CameraOpticsMode are now defined in ────────
// camera_projection_source.hpp so that the projection pipeline can depend on
// the interface without pulling in the full camera_2_5d.hpp header.

struct Camera2_5D {
    bool enabled{false};

    // Set by the RenderPlan camera-track adapter when authored camera values
    // vary over time. Read by SceneHasher::camera_is_static() to decide whether
    // the camera is time-dependent for fast-path and caching purposes.
    // Defaults to false (static camera).
    bool is_animated{false};

    // Camera position in screen-space world coordinates.
    // Default: 1000 units "in front" of the z=0 plane.
    Vec3 position{0.0f, 0.0f, -1000.0f};

    // Projection mode: Zoom (default) or Fov.  Legacy selector kept for
    // backward compatibility (composers that still want Camera2_5D level
    // control over the projection).  See CameraOpticsMode on the rig for
    // the canonical optics contract used by the projection pipeline.
    // REMOVED: Camera2_5DProjectionMode projection_mode — replaced by optics_mode.

    // Optics mode — the canonical contract used by the projection pipeline.
    // Decoupled from DoF: a physical lens can be active (PhysicalLens)
    // while DoF.enabled is false.
    CameraOpticsMode optics_mode{CameraOpticsMode::Zoom};

    // Perspective strength. At depth == zoom, perspective_scale == 1.
    // Focal-length zoom value (px). Used when optics_mode == Zoom.
    f32 zoom{1000.0f};

    // Field of view in degrees. Used when optics_mode == FieldOfView.
    // 35° ≈ telephoto (less distortion), 70° ≈ wide angle (more parallax).
    f32 fov_deg{50.0f};

    // Principal-point offset as a fraction of viewport dimensions.
    // {0,0} is centered; positive X moves right and positive Y moves down.
    // Values may exceed half the viewport to support off-screen framing.
    Vec2 lens_shift{0.0f, 0.0f};

    // Resolved renderer pose. Cinematic look-at and parent constraints are
    // evaluated by ChrononMotion3D before this state reaches the renderer.
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

    // Motion blur settings — populated by the RenderPlan camera adapter. The
    // render pipeline prefers camera-level
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

    [[nodiscard]] Mat4 view_matrix() const {
        // The pose is already resolved upstream; the renderer only turns it
        // into a view transform and never derives orientation from a target.
        return math::camera_view_matrix(position, rotation_quaternion());
    }
};

using Camera2_5DRuntime = Camera2_5D;

} // namespace chronon3d
