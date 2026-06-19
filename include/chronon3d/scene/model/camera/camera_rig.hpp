#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>
// Camera2_5D is forward-declared by camera_projection_source.hpp.
// Complete type needed only in camera_rig.cpp.
// AnimatedCamera2_5D is not needed by the struct definitions — only by the
// presets header (camera_rig_animated_presets.hpp).
#include <memory_resource>
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

    // ── Canonical optics contract (mirrors the modern CameraRig) ───────────
    // CameraOpticsMode is decoupled from DoF: a physical lens can be active
    // (PhysicalLens) while DepthOfFieldSettings::use_physical_model is false.
    // It is mirrored to the evaluated Camera2_5D by bake() and read by the
    // projection pipeline (focal_from_camera with discrete switch).
    CameraOpticsMode optics_mode{CameraOpticsMode::Zoom};

    // ── Single-switch focus contract (mirrors the modern CameraRig) ────────
    // Canonical focus ownership selector.  Defaults to ManualDistance for
    // backward compatibility with the legacy direct-distance path.  The
    // single switch on focus_mode in bake() is responsible for assigning
    // cam.dof.focus_distance exactly once.
    //
    //   ManualDistance:    focus_distance == dof.focus_distance (legacy default).
    //   PointOfInterest:   focus_distance == |cam.point_of_interest - cam.position|.
    //   TargetLayer:       focus_distance == |world(t) - cam.position|
    //                      (legacy has one target_name, so it maps to the
    //                      rig target's world position — same as POI).
    //   LockToZoom:        focus_distance == cam.zoom (rack / dolly zoom).
    CameraFocusMode focus_mode{CameraFocusMode::ManualDistance};

    // ── Conservative invalidation helpers (mirror the modern CameraRig) ───
    // True iff the rig depends on external state (controller_name, target_name)
    // whose world position may move even when no local AnimatedValue is
    // time-dependent.  Used by the caching pipeline to invalidate the
    // camera fingerprint.
    [[nodiscard]] bool has_external_dependencies() const noexcept {
        return !controller_name.empty() || !target_name.empty();
    }

    // True iff any local AnimatedValue on the rig is time-dependent.
    [[nodiscard]] bool is_time_dependent_any() const noexcept {
        return controller_position.is_time_dependent() ||
               controller_rotation.is_time_dependent() ||
               controller_anchor.is_time_dependent() ||
               target_position.is_time_dependent() ||
               target_rotation.is_time_dependent() ||
               target_anchor.is_time_dependent() ||
               camera_position.is_time_dependent() ||
               camera_rotation.is_time_dependent() ||
               zoom.is_time_dependent() ||
               fov_deg.is_time_dependent();
    }

    // ── bake() — canonical chain + single-switch focus (mirrors modern rig) ─
    //
    // TwoNode path:
    //   q = qLookAt * qLocal * qX(tilt) * qY(pan) * qZ(roll)
    //   (composed by Camera2_5D::resolve_look_at_orientation())
    //
    // OneNode path:
    //   q = qLocal * qX(tilt) * qY(pan) * qZ(roll)
    //   (POI is disabled; user-supplied camera_rotation drives the view)
    //
    // Focus: a single switch on focus_mode is responsible for focal
    // distance — no later re-assignment of cam.dof.focus_distance is
    // permitted by this function.
    //
    // NOTE: cam.point_of_interest and cam.is_animated are deliberately
    // left under the responsibility of downstream consumers (hierarchy resolver,
    // cache pipeline) — the legacy bake() returns a "minimal wiring" Camera2_5D
    // and lets them promote values to world space to preserve existing legacy
    // golden-render contracts.  When the caller supplies a non-null `resolved`
    // and opt-in flags warrant POI pre-population, the modern rig's stricter
    // version can be used.
    [[nodiscard]] Camera2_5D bake(
        Frame frame,
        std::pmr::memory_resource* res = std::pmr::get_default_resource(),
        [[maybe_unused]] const ResolvedSceneTransforms* resolved = nullptr
    ) const;

    void apply(SceneBuilder& s, Frame frame, std::function<void(SceneBuilder&)> add_target_content) const;
    void apply(SceneBuilder& s, Frame frame) const;
};

} // namespace camera_rig

} // namespace chronon3d

// ── Animated presets moved to camera_rig_animated_presets.hpp ───────────────
// The inline preset helpers (hero_push_in, orbit_yaw, etc.) that return
// AnimatedCamera2_5D are now in their own header so that camera_rig.hpp
// doesn't need to include animated_camera_2_5d.hpp.
//
// #include <chronon3d/scene/camera/camera_rig_animated_presets.hpp>

// (end of camera_rig.hpp)
