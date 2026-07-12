#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_descriptor.hpp
//
// CameraDescriptor — authoring-time data that fully describes a camera.
//
// This is the *only* camera authoring form going forward. Every preset,
// script, or composition creates a CameraDescriptor, then calls
// compile_camera() to produce an immutable CameraProgram.
//
// The variant-based source, orientation, and constraint types replace:
//   - AnimatedCamera2_5D         → PoseTracksSource
//   - CameraRig (modern)          → OrbitMotion
//   - CameraMotionParams          → PoseTracksSource + IdleOscillation
//   - camera_motion::dolly/pan    → preset descriptor
//   - OrientationPolicy enum      → OrientationSpec variant
//   - CameraConstraintSpec (PR6+) → replaces CameraConstraintRegistry
//
// Layout:
//   CameraDescriptor
//     ├── CameraBaseSpec       (non-motion base state)
//     ├── CameraSourceSpec     (variant: static / pose-tracks / orbit / trajectory / ref)
//     ├── modifiers            (IdleOscillation etc.)
//     ├── OrientationSpec      (variant: fixed / look-at-point / look-at-layer / along-path)
//     └── failure_policy
//
// NOTE: This header does NOT include camera_program.hpp to avoid circular
// dependency. CameraFailurePolicy is defined here. camera_program.hpp
// includes this header instead.
// ==============================================================================
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>    // CameraTrajectory
#include <chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp>  // TICKET-FRAMING-V1: FramingBBox
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>             // Camera2_5D, DepthOfFieldSettings, MotionBlurSettings, LensModel

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace chronon3d::camera_v1 {

// =============================================================================
// CameraFailurePolicy — controls failure behaviour during constraint evaluation.
// =============================================================================

/// Controls how the program reacts to a failing constraint.
enum class CameraFailurePolicy : std::uint8_t {
    Stop = 0,                 // bail out on first constraint failure (default)
    SkipFailedConstraint = 1, // skip the failing constraint, continue the stack
    KeepLastValidCamera = 2,  // return last camera that passed all constraints
};

// =============================================================================
// ProjectionSpec — variant avoids ambiguous zoom + FOV state.
// =============================================================================

/// Perspective defined by a zoom value (focal length ≈ zoom).
struct ZoomProjection {
    AnimatedValue<float> zoom{AnimatedValue<float>{1000.0f}};
};

/// Perspective defined by a vertical field of view (degrees).
struct FovProjection {
    AnimatedValue<float> fov_deg{AnimatedValue<float>{50.0f}};
};

/// Perspective defined by a physical lens (focal length, sensor, gate-fit).
///
/// CAM-03 / DOC 02 — anamorphic behaviour is expressed entirely through the
/// `LensModel` (sensor dimensions + gate-fit).  See `LensPresets::anamorphic_50mm()`
/// for a real anamorphic preset; use that as `lens` here. A future PR will
/// surface explicit `anamorphic_squeeze` and `pixel_aspect` on Camera2_5D;
///// for now the variant only carries the lens and the optics-mode switch.
struct PhysicalLensProjection {
    LensModel lens{LensPresets::full_frame_50mm()};
};

using ProjectionSpec = std::variant<ZoomProjection, FovProjection, PhysicalLensProjection>;

// =============================================================================
// CameraBaseSpec — non-motion camera state.
//
// Fields here are NOT animated via AnimatedValue; they serve as the base /
// fallback for the camera.  Motion sources may override position, rotation,
// zoom, or FOV dynamically.
// =============================================================================

struct CameraBaseSpec {
    bool    enabled{true};
    Vec3    position{0.0f, 0.0f, -1000.0f};

    // TICKET-CAM-QUAT-PRIMARY: Quat orientation is now the PRIMARY
    // orientation state (avoids 179° → -179° jumps, gimbal lock, and
    // long-rotation interpolation issues of Euler). The `rotation`
    // field below is kept as a backward-compat Euler accessor — set
    // `rotation` mirrors into `orientation` (and vice versa) via the
    // `set_rotation_euler()` / `rotation_euler()` accessors.  Existing
    // aggregate-init / .set() call sites continue to compile (the
    // `rotation` field stays first; `orientation` defaults to identity).
    Vec3    rotation{0.0f, 0.0f, 0.0f};
    Quat    orientation{1.0f, 0.0f, 0.0f, 0.0f};

    ProjectionSpec projection{ZoomProjection{AnimatedValue<float>{1000.0f}}};

    LensModel               lens{50.0f, 2.8f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    DepthOfFieldSettings    dof{};
    MotionBlurSettings      motion_blur{};

    std::string parent_name;
    bool        point_of_interest_enabled{false};
    Vec3        point_of_interest{0.0f, 0.0f, 0.0f};

    // TICKET-FRAMING-V1: opt-in framing targets.  When non-empty, the
    // evaluator runs a 5th stage (framing) AFTER constraints and BEFORE
    // the final return.  Each FramingBBox is a world-space AABB; the
    // solver picks the camera position + aim that frames all targets
    // within the safe area + rule-of-thirds + dead-zone constraints.
    // HONEST GAP: the per-layer "real bounds" query (the user spec asks
    // for "Usa i bounds REALI dei layer NON tabelle manuali") is NOT
    // implemented — the evaluator reads the targets from this field, which
    // the composition author fills in at descriptor-build time.  The
    // real-bounds query (against `ctx.transforms` or a new scene-bounds
    // resolver) is catalogued as a follow-up forward-point in
    // `docs/FOLLOWUP_TICKETS.md` §Catalogued forward-points.
    std::vector<FramingBBox> framing_targets;

    // TICKET-FRAMING-V1 — composition_point + look_ahead (user-spec verbatim).
    // composition_point is the desired screen-space anchor for the centroid
    // (normalised [0,1] coords; default 0.5/0.5 = centre; the RuleOfThirds
    // strategy reuses this as the bias from the geometric centre).  look_ahead
    // is the velocity look-ahead in seconds (default 0.0 = disabled; the
    // solver projects the target's motion Δt seconds into the future before
    // computing the camera aim).
    Vec2  composition_point{0.5f, 0.5f};
    float look_ahead{0.0f};
};

// =============================================================================
// CameraSourceSpec — what drives the camera animation.
// =============================================================================

/// No animation; camera stays at its base state.
struct StaticCameraSource {};

/// Keyframed position / rotation / target / zoom / FOV.
/// Replaces AnimatedCamera2_5D and the imperative camera_rig::* helpers
/// (hero_push_in, orbit_yaw, parallax_pan, dolly_zoom, focus_pull,
///  low_angle_reveal, subtle_float).
///
/// The AnimatedValue fields hold keyframes; evaluate() at a given frame
/// produces the interpolated pose.
struct PoseTracksSource {
    AnimatedValue<Vec3> position{Vec3{0.0f, 0.0f, -1000.0f}};
    AnimatedValue<Vec3> rotation{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> target{Vec3{0.0f, 0.0f, 0.0f}};

    AnimatedValue<float> zoom{AnimatedValue<float>{1000.0f}};
    AnimatedValue<float> fov_deg{AnimatedValue<float>{50.0f}};

    // DOF channels — animated depth of field.
    // When a channel has no keyframes, eval_pose_tracks() falls back to
    // the static values in CameraBaseSpec::dof.
    AnimatedValue<float> focus_distance{AnimatedValue<float>{1000.0f}};
    AnimatedValue<float> aperture{AnimatedValue<float>{0.015f}};
    AnimatedValue<float> max_blur{AnimatedValue<float>{24.0f}};

    bool use_target{false};
};

/// Orbit around a target with yaw/pitch/radius + track/dolly/roll.
/// Replaces the modern CameraRig (in namespace chronon3d).
///
///     yaw     = horizontal orbit angle (degrees)
///     pitch   = vertical orbit angle (degrees)
///     radius  = distance from target
///     track   = lateral offset from orbit center (local xz plane)
///     dolly   = additional push along the forward axis
///     roll    = camera roll (degrees)
///
/// All channels are keyframable via AnimatedValue.
struct OrbitMotion {
    AnimatedValue<Vec3>  target{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<float> yaw{AnimatedValue<float>{0.0f}};
    AnimatedValue<float> pitch{AnimatedValue<float>{0.0f}};
    AnimatedValue<float> radius{AnimatedValue<float>{1000.0f}};

    AnimatedValue<Vec3>  track{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<float> dolly{AnimatedValue<float>{0.0f}};
    AnimatedValue<float> roll{AnimatedValue<float>{0.0f}};
};

/// A pre-built trajectory (sequence of segments, arc-length parameterised).
struct TrajectoryMotion {
    std::shared_ptr<CameraTrajectory> trajectory;
    bool use_arc_length{true};
};

/// Reference to a named preset in the CameraCatalog.
/// Resolved to a concrete source at compile_camera() time.
struct RegisteredMotionRef {
    std::string id;
};

using CameraSourceSpec = std::variant<
    StaticCameraSource,
    PoseTracksSource,
    OrbitMotion,
    TrajectoryMotion,
    RegisteredMotionRef
>;

// =============================================================================
// Modifier specs — additive post-source effects.
// =============================================================================

/// Sinusoidal idle oscillation (replaces CameraMotionIdle).
struct IdleOscillation {
    Vec3  position_amplitude{0.0f, 0.0f, 0.0f};
    Vec3  rotation_amplitude_deg{0.0f, 0.0f, 0.0f};
    float zoom_amplitude{0.0f};
    float frequency_hz{0.15f};
    float phase{0.0f};
};

/// Handheld noise (CAM-04 / DOC 03) — wiggle3D-based per-axis displacement
/// driven by ABSOLUTE time (`ctx.sample_time.seconds()`). Same absolute
/// time ⇒ identical offset regardless of evaluation order, giving
/// strong deterministic-render guarantees.  Inspired by `ShakePreset::handheld`
/// but rebuilt around abs-time to ensure cross-stitch / random-access
/// determinism (DOC 03 §5 — random-access determinism).
struct HandheldNoise {
    Vec3       position_amplitude{2.0f, 1.0f, 0.5f};
    Vec3       rotation_amplitude_deg{0.5f, 0.3f, 0.2f};
    float      zoom_amplitude{0.0f};
    float      position_freq_hz{4.0f};
    float      rotation_freq_hz{3.0f};
    float      zoom_freq_hz{1.0f};
    std::uint32_t seed{42};
};

/// Placeholder for future additive-track modifiers.
// struct AdditivePoseTrack {};

using CameraModifierSpec = std::variant<
    IdleOscillation,
    HandheldNoise
>;

// =============================================================================
// OrientationSpec — variant with data, replacing OrientationPolicy enum.
// =============================================================================

/// Keep the camera's existing rotation (as set by source).
struct FixedOrientation {};

/// Orient to always look at a fixed world-space point.
struct LookAtPoint {
    Vec3 target{0.0f, 0.0f, 0.0f};
};

/// Orient to always look at a named layer's world position.
/// Requires a transform snapshot in CameraEvalContext to resolve.
struct LookAtLayer {
    std::string target;
};

/// Orient the camera forward along its trajectory tangent.
struct OrientAlongPath {
    bool keep_horizon{false};   // if true, zero out roll
};

using OrientationSpec = std::variant<
    FixedOrientation,
    LookAtPoint,
    LookAtLayer,
    OrientAlongPath
>;

// =============================================================================
// CameraConstraintSpec — typed constraint parameters (PR7).
// For PR1 the variant is declared but the compiler defers to the existing
// CameraConstraintSpec (compiled path, PR6+).
// =============================================================================

// Forward declare constraint parameter structs (defined in camera_constraint.hpp).
struct LookAtConstraint       { Vec3 target{0,0,0}; };
struct KeepHorizonConstraint  {};
struct DampedFollowConstraint { float damping{0.15f}; };
struct DistanceConstraint     { float min_distance{10.0f}; float max_distance{10000.0f}; };
struct RotationLimitConstraint{ float max_pitch_deg{90.0f}; float max_yaw_deg{180.0f}; float max_roll_deg{45.0f}; };

using CameraConstraintSpec = std::variant<
    LookAtConstraint,
    KeepHorizonConstraint,
    DampedFollowConstraint,
    DistanceConstraint,
    RotationLimitConstraint
>;

// =============================================================================
// CameraDescriptor — top-level authoring descriptor.
// =============================================================================

/// Complete description of a camera, ready to be compiled into a CameraProgram.
struct CameraDescriptor {
    std::string id;

    CameraBaseSpec                     base;
    CameraSourceSpec                   source{StaticCameraSource{}};
    std::vector<CameraModifierSpec>    modifiers;
    OrientationSpec                    orientation{FixedOrientation{}};
    std::vector<CameraConstraintSpec>  constraints;

    CameraFailurePolicy failure_policy{CameraFailurePolicy::Stop};
};

} // namespace chronon3d::camera_v1

// ── Fingerprint split to camera_descriptor_fingerprint.hpp (STEP 8) ────────
// For backward compatibility, include the new header here.
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>
