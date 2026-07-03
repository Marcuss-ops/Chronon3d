#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>
// Camera2_5D is forward-declared by camera_projection_source.hpp.
// Complete type needed only in camera_rig.cpp.
// AnimatedCamera2_5D is not needed by the struct definitions — only by the
// presets header (camera_rig_animated_presets.hpp).
//
// TICKET-026 — `CameraRigMotionBlur` keeps `MotionBlurSettings::mode`
// by-value (canonical), so the `MotionBlurMode` enum must be visible
// at struct-definition time.  `camera_2_5d.hpp` is the canonical owner
// and does not include `camera_rig.hpp` back, so this include is safe
// (no circular).  Only the enum + MotionBlurSettings struct are reached
// here; the heavyweight Camera2_5D view-matrix/renderers stay in the .cpp.
#include <memory_resource>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>  // TICKET-026 — MotionBlurMode visibility.
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms replaces the legacy TransformResolverResult.
#include <string>
#include <utility>
#include <functional>

namespace chronon3d {
class SceneBuilder;

enum class CameraRigMode {
    OneNode,
    TwoNode
};

// ── CameraFocusMode — who owns the focus distance ──────────────────────────
//
// Public contract for the focus distance resolution.  The previous
// implementation in CameraRig::evaluate() always evaluated the manual
// AnimatedValue last, which silently overwrote the target-derived distance
// even when the target binding was selected.  This enum makes the ownership
// explicit and unifies the resolution in a single switch.
//
//   ManualDistance:        focus_distance.evaluate(time) wins; target is ignored.
//   PointOfInterest:       focus_distance is set to the rig target's camera-space distance.
//   TargetLayer:           focus_distance is set to focus_target_name's camera-space distance.
//                          Falls back to PointOfInterest if the named layer is missing.
//   LockToZoom:            focus_distance == zoom (rack focus follows the camera focal length).
enum class CameraFocusMode {
    ManualDistance,    // default — honour the user-supplied focus_distance.
    PointOfInterest,   // distance to the rig target.
    TargetLayer,       // distance to the focus_target_name layer (POI fallback).
    LockToZoom,        // distance == camera zoom (used by dolly zoom).
};

struct CameraRigDOF {
    bool enabled{false};
    std::string focus_target_name;

    // Canonical focus ownership selector.  Defaults to ManualDistance for
    // backward compatibility with the legacy use_target_z == false path.
    CameraFocusMode focus_mode{CameraFocusMode::ManualDistance};

    // Legacy model.
    AnimatedValue<f32> focus_z{0.0f};
    AnimatedValue<f32> aperture{0.015f};
    AnimatedValue<f32> max_blur{24.0f};
    // use_target_z is now deprecated: it is mapped onto CameraFocusMode
    // by CameraRig::evaluate() so existing code keeps compiling.
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

// TICKET-026 — `bool enabled` removed; the rig-side struct now mirrors the
// canonical MotionBlurSettings (mode + the 6 numeric/pattern fields).
// CameraRigMotionBlur is the per-rig authoring form; MotionBlurSettings is
// the per-frame Camera2_5D carrier.  The historical one-to-one plumbing
// `cam.motion_blur.enabled = motion_blur.enabled` becomes
// `cam.motion_blur.mode = motion_blur.mode` and the per-numeric-field copy
// loop in camera_rig.cpp is unchanged.
struct CameraRigMotionBlur {
    MotionBlurMode mode{MotionBlurMode::Off};
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

    // Legacy projection selector kept for backward compatibility (composers
    // that drive Camera2_5D directly).  The canonical optics contract lives
    // on `optics_mode` and is what the projection pipeline reads.
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    // CameraOpticsMode — decoupled from DoF so a physical lens can be active
    // without enabling depth of field.  Mirrored to the evaluated Camera2_5D
    // by CameraRig::evaluate().
    CameraOpticsMode optics_mode{CameraOpticsMode::Zoom};

    CameraRigDOF dof{};
    CameraRigMotionBlur motion_blur{};

    [[nodiscard]] Camera2_5D evaluate(
        Frame frame,
        const ResolvedSceneTransforms* resolved = nullptr
    ) const;

    /// Sub-frame evaluation — enables true multi-sample motion blur.
    [[nodiscard]] Camera2_5D evaluate(
        SampleTime time,
        const ResolvedSceneTransforms* resolved = nullptr
    ) const;

    /// Returns true iff the rig depends on external state (parent_name,
    /// target_name, focus_target_name) whose world position may move even
    /// when no local AnimatedValue is time-dependent.  Used by the caching
    /// pipeline to invalidate the camera cache/fingerprint.
    [[nodiscard]] bool has_external_dependencies() const noexcept {
        return !parent_name.empty() || !target_name.empty() || !dof.focus_target_name.empty();
    }
};

} // namespace chronon3d

// ── Legacy camera_rig::CameraRig REMOVED (STEP 7 dead-code elimination).
// The modern chronon3d::CameraRig is the canonical authoring type.

// (end of camera_rig.hpp)
