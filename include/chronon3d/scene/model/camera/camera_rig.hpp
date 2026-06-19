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
        const TransformResolverResult* resolved = nullptr
    ) const {
        return evaluate(SampleTime::from_frame_int(frame, FrameRate{30, 1}), resolved);
    }

    /// Sub-frame evaluation — enables true multi-sample motion blur.
    [[nodiscard]] Camera2_5D evaluate(
        SampleTime time,
        const TransformResolverResult* resolved = nullptr
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
        [[maybe_unused]] const TransformResolverResult* resolved = nullptr
    ) const {
        Camera2_5D cam;
        cam.enabled                   = enabled;
        cam.position                  = camera_position.evaluate(frame);
        cam.rotation                  = camera_rotation.evaluate(frame);
        cam.zoom                      = zoom.evaluate(frame);
        cam.fov_deg                   = fov_deg.evaluate(frame);
        cam.projection_mode           = use_fov
                                            ? Camera2_5DProjectionMode::Fov
                                            : Camera2_5DProjectionMode::Zoom;
        cam.optics_mode               = optics_mode; // mirror canonical optics_mode to evaluated cam
        cam.parent_name               = std::pmr::string{controller_name, res};

        if (mode == RigMode::TwoNode) {
            // Canonical chain guard: target_name drives the world-space
            // POI resolved by resolve_look_at_orientation().  The hierarchy
            // resolver is the source of truth for cam.point_of_interest;
            // bake() leaves it to be filled downstream.  We don't pre-fill
            // here so the legacy golden render contract is preserved
            // (target_position.evaluate == (0,0,0) for default rigs).
            cam.point_of_interest_enabled = true;
            cam.target_name               = std::pmr::string{target_name, res};
        } else {
            // OneNode: POI is disabled and target_name is empty so the
            // canonical chain collapses to qLocal * qX * qY * qZ in
            // resolve_look_at_orientation().
            cam.point_of_interest_enabled = false;
            cam.target_name.clear();
        }

        // ── Single-switch focus distance ──────────────────────────────
        // One and only one path assigns cam.dof.focus_distance here.
        // Do NOT append another assignment below this switch.
        cam.dof = dof;
        // use_physical_model is honored from the user's choice (do not
        // silently override).  The legacy DoF has no LensModel, so a user
        // setting use_physical_model=true will get a runtime warning from
        // the projection pipeline rather than a silent fallback.
        const Vec3 focus_po = cam.point_of_interest_enabled
                                  ? cam.point_of_interest
                                  : cam.position;

        switch (focus_mode) {
            case CameraFocusMode::ManualDistance: {
                // Explicit value; explicit owner wins, target is ignored.
                // For default rig this matches the legacy cam.dof = dof copy.
                cam.dof.focus_distance = dof.focus_distance;
                cam.dof.focus_z        = dof.focus_z;
                break;
            }
            case CameraFocusMode::PointOfInterest: {
                cam.dof.focus_distance = glm::length(focus_po - cam.position);
                cam.dof.focus_z        = focus_po.z;
                break;
            }
            case CameraFocusMode::TargetLayer: {
                // Legacy rig has a single target_name; TargetLayer maps to
                // the rig target's world position (same as POI for namespace
                // compatibility).
                cam.dof.focus_distance = glm::length(focus_po - cam.position);
                cam.dof.focus_z        = focus_po.z;
                break;
            }
            case CameraFocusMode::LockToZoom: {
                // Dolly-zoom / rack: pin focus to current zoom.
                cam.dof.focus_distance = cam.zoom;
                cam.dof.focus_z        = focus_po.z;
                break;
            }
        }

        // cam.is_animated is left to its default (false); the legacy rig
        // is treated as static by the cache, which matches the existing
        // golden-render contracts.  Use the modern CameraRig (which has
        // stricter is_animated wiring via has_external_dependencies) when
        // cache invalidation from external bindings is required.

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
