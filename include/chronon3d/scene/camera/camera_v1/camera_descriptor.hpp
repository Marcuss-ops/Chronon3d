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
//   - CameraConstraintRegistry    → CameraConstraintSpec variant (PR7)
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

using ProjectionSpec = std::variant<ZoomProjection, FovProjection>;

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
    Vec3    rotation{0.0f, 0.0f, 0.0f};

    ProjectionSpec projection{ZoomProjection{AnimatedValue<float>{1000.0f}}};

    LensModel               lens{50.0f, 2.8f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    DepthOfFieldSettings    dof{};
    MotionBlurSettings      motion_blur{};

    std::string parent_name;
    bool        point_of_interest_enabled{false};
    Vec3        point_of_interest{0.0f, 0.0f, 0.0f};
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

/// Placeholder for future handheld / additive-track modifiers.
// struct HandheldNoise {};
// struct AdditivePoseTrack {};

using CameraModifierSpec = std::variant<
    IdleOscillation
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
// CameraConstraintRegistry until the migration in PR7.
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
