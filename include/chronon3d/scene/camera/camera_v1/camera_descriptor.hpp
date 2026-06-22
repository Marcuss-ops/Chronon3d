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

// ==============================================================================
// CAM-02: compute_camera_descriptor_fingerprint
//
// Deterministic, address-independent hash of a CameraDescriptor.  Two
// descriptors with the same content (incl. two CameraTrajectory instances
// with the same point/segment tables, but held by different shared_ptrs)
// hash to the same value.  Heap-allocated identities (shared_ptr, vector
// capacities, padding) are NOT hashed — only the *content* (variant
// discriminant, all visible fields, keyframe counts/frames/values, etc.).
//
// Field coverage (per user spec for CAM-02):
//   - source                  ✓
//   - orientation             ✓
//   - constraints             ✓
//   - lens                    ✓
//   - base (position/rotation/projection/dof/motion_blur/parent_name/poi)
//   - modifiers               ✓
//   - failure_policy          ✓
//
// Note: "transition spec" is not a CameraDescriptor field.  Transitions
// live in shot_timeline.hpp::CameraShot.transition_out / .transition_frames
// and are intentionally NOT fingerprintable here (different surface).
//
// DETERMINISM NOTES:  the FNV-1a hash state is initialised to its
// canonical offset basis.  Float-typed fields are hashed as their IEEE-754
// bit representation via memcpy to guarantee NaN / sub-normal
// reproducibility across runs.  Iteration order is structural order of
// fields (declaration order in CameraDescriptor and its variants).
// ==============================================================================
#include <cstdint>
#include <cstring>

namespace chronon3d::camera_v1 {

namespace detail {

/// FNV-1a 64-bit streaming hasher.  simple, deterministic, bit-stable.
class Fnv1aHasher {
public:
    std::uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a 64 offset basis.
    static constexpr std::uint64_t kPrime = 0x100000001b3ULL;

    void mix_bytes(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= kPrime;
        }
    }

    void mix_u8 (std::uint8_t  v) { mix_bytes(&v, sizeof(v)); }
    void mix_u32(std::uint32_t v) { mix_bytes(&v, sizeof(v)); }
    void mix_u64(std::uint64_t v) { mix_bytes(&v, sizeof(v)); }
    void mix_f32(float         v) { mix_bytes(&v, sizeof(v)); }
    void mix_i32(std::int32_t   v) { mix_bytes(&v, sizeof(v)); }

    void mix_str(std::string_view s) {
        mix_u64(s.size());
        if (!s.empty()) mix_bytes(s.data(), s.size());
    }
    void mix_bool(bool b) { mix_u8(b ? 1u : 0u); }
    void mix_vec3(const Vec3& v) { mix_f32(v.x); mix_f32(v.y); mix_f32(v.z); }

    template<typename E>
    void mix_enum(E v) { mix_u32(static_cast<std::uint32_t>(v)); }
};

/// Hash the *content* of an AnimatedValue (excludes any internal padding,
/// addresses, cap).  is_time_dependent + keyframe count + per-keyframe
/// (frame, value bytes, interp mode, easing curve, roving).
///
/// EasingCurve hashing: EasingCurve has an Easing enum + optional
/// CubicBezier (4 floats).  Two descriptors with identical frames+values
/// but different Easings MUST hash differently — the easing materially
/// affects the evaluated output, so for fingerprint determinism we mix:
///   - the Easing preset enum (1 byte cast)
///   - whether a CubicBezier is present (boolean)
///   - the four CubicBezier control-point scalars (if present)
template <typename T>
inline void hash_animated_value(Fnv1aHasher& h,
                                const chronon3d::AnimatedValue<T>& av) {
    h.mix_bool(av.is_time_dependent());
    const auto& kfs = av.keyframes();
    h.mix_u64(kfs.size());
    for (const auto& kf : kfs) {
        // Use the value itself rather than the .value field (Frame is a
        // struct holding an `int`; mix its raw int32).
        h.mix_i32(kf.frame.value);
        h.mix_bytes(&kf.value, sizeof(T));
        h.mix_enum(kf.interp);
        h.mix_bool(kf.roving);
        // Hash the EasingCurve (preset + optional CubicBezier control
        // points).  Closed over e.g. an `Easing::Linear` + no cubic ⇒
        // only the enum value is mixed.
        h.mix_enum(kf.easing.preset);
        h.mix_bool(kf.easing.cubic.has_value());
        if (kf.easing.cubic.has_value()) {
            h.mix_f32(kf.easing.cubic->x1);
            h.mix_f32(kf.easing.cubic->y1);
            h.mix_f32(kf.easing.cubic->x2);
            h.mix_f32(kf.easing.cubic->y2);
        }
    }
}

} // namespace detail

inline std::uint64_t compute_camera_descriptor_fingerprint(
    const CameraDescriptor& desc) {
    detail::Fnv1aHasher h;

    // ── Identifier + policy ────────────────────────────────────────
    h.mix_str(desc.id);
    h.mix_enum(desc.failure_policy);

    // ── base spec ─────────────────────────────────────────────────
    h.mix_bool(desc.base.enabled);
    h.mix_vec3(desc.base.position);
    h.mix_vec3(desc.base.rotation);
    h.mix_bool(desc.base.point_of_interest_enabled);
    h.mix_vec3(desc.base.point_of_interest);
    h.mix_str(desc.base.parent_name);

    // ── Lens / DOF / motion blur: hash by individual fields rather than
    // ── struct-bytes.  All three (LensModel, DepthOfFieldSettings,
    // ── MotionBlurSettings) are standard-layout POD aggregations with
    // ── only f32 / bool / enum members — no pointers, no std::string,
    // ── no inner allocations.  Per-field hashing is deterministic
    // ── across compilers, unaffected by padding, and pinpoints which
    // ── field changed if two hashes diverge.
    {
        const auto& lens = desc.base.lens;
        h.mix_f32(lens.focal_length);
        h.mix_f32(lens.f_stop);
        h.mix_f32(lens.close_focus);
        h.mix_f32(lens.sensor_width);
        h.mix_f32(lens.sensor_height);
        h.mix_enum(lens.gate_fit);
    }
    {
        const auto& dof = desc.base.dof;
        h.mix_bool(dof.enabled);
        h.mix_f32(dof.focus_z);
        h.mix_f32(dof.aperture);
        h.mix_f32(dof.max_blur);
        h.mix_f32(dof.focus_distance);
        h.mix_bool(dof.use_physical_model);
        h.mix_f32(dof.near_bokeh_radius);
        h.mix_f32(dof.far_bokeh_radius);
    }
    {
        const auto& mb = desc.base.motion_blur;
        // TICKET-026 — `mb.enabled` removed; mode is the canonical
        // active indicator.  mix_u8(<enum index>) keeps the fingerprint
        // byte-stable across identical mode values and distinguishes Off
        // from any enabled mode (TemporalAccumulation/VelocityApproximation).
        h.mix_u8(static_cast<std::uint8_t>(mb.mode));
        h.mix_f32(mb.shutter_angle_deg);
    }

    // ── projection variant ────────────────────────────────────────
    h.mix_u8(desc.base.projection.index());
    std::visit([&h](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, ZoomProjection>) {
            detail::hash_animated_value(h, p.zoom);
        } else if constexpr (std::is_same_v<T, FovProjection>) {
            detail::hash_animated_value(h, p.fov_deg);
        }
    }, desc.base.projection);

    // ── source variant ────────────────────────────────────────────
    h.mix_u8(desc.source.index());
    std::visit([&h](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, StaticCameraSource>) {
            // No fields.
        } else if constexpr (std::is_same_v<T, PoseTracksSource>) {
            detail::hash_animated_value(h, s.position);
            detail::hash_animated_value(h, s.rotation);
            detail::hash_animated_value(h, s.target);
            detail::hash_animated_value(h, s.zoom);
            detail::hash_animated_value(h, s.fov_deg);
            detail::hash_animated_value(h, s.focus_distance);
            detail::hash_animated_value(h, s.aperture);
            detail::hash_animated_value(h, s.max_blur);
            h.mix_bool(s.use_target);
        } else if constexpr (std::is_same_v<T, OrbitMotion>) {
            detail::hash_animated_value(h, s.target);
            detail::hash_animated_value(h, s.yaw);
            detail::hash_animated_value(h, s.pitch);
            detail::hash_animated_value(h, s.radius);
            detail::hash_animated_value(h, s.track);
            detail::hash_animated_value(h, s.dolly);
            detail::hash_animated_value(h, s.roll);
        } else if constexpr (std::is_same_v<T, TrajectoryMotion>) {
            // EXCLUDE pointer identity; HASH only trajectory content.
            h.mix_bool(s.use_arc_length);
            if (s.trajectory) {
                h.mix_u64(s.trajectory->size());
                for (const auto& p : s.trajectory->points()) {
                    h.mix_vec3(p.position);
                    h.mix_vec3(p.handle_in);
                    h.mix_vec3(p.handle_out);
                    h.mix_bool(p.target.has_value());
                    if (p.target) h.mix_vec3(*p.target);
                    h.mix_f32(p.roll_deg);
                }
                for (const auto& seg : s.trajectory->segments()) {
                    h.mix_u8(static_cast<std::uint8_t>(seg.kind));
                    h.mix_u64(seg.from_idx);
                    h.mix_u64(seg.to_idx);
                    h.mix_f32(seg.duration_frames);
                }
            } else {
                // Sentinel for "null shared_ptr" — distinct from any size.
                h.mix_u64(0xffffffffffffffffULL);
            }
        } else if constexpr (std::is_same_v<T, RegisteredMotionRef>) {
            // Hash id only — no address / no descriptor copy here.
            h.mix_str(s.id);
        }
    }, desc.source);

    // ── orientation variant ────────────────────────────────────────
    h.mix_u8(desc.orientation.index());
    std::visit([&h](const auto& o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, FixedOrientation>) {
            // No fields.
        } else if constexpr (std::is_same_v<T, LookAtPoint>) {
            h.mix_vec3(o.target);
        } else if constexpr (std::is_same_v<T, LookAtLayer>) {
            h.mix_str(o.target);
        } else if constexpr (std::is_same_v<T, OrientAlongPath>) {
            h.mix_bool(o.keep_horizon);
        }
    }, desc.orientation);

    // ── modifiers ─────────────────────────────────────────────────
    h.mix_u64(desc.modifiers.size());
    for (const auto& m : desc.modifiers) {
        h.mix_u8(m.index());
        std::visit([&h](const auto& mod) {
            using T = std::decay_t<decltype(mod)>;
            if constexpr (std::is_same_v<T, IdleOscillation>) {
                h.mix_vec3(mod.position_amplitude);
                h.mix_vec3(mod.rotation_amplitude_deg);
                h.mix_f32(mod.zoom_amplitude);
                h.mix_f32(mod.frequency_hz);
                h.mix_f32(mod.phase);
            }
        }, m);
    }

    // ── constraints ───────────────────────────────────────────────
    h.mix_u64(desc.constraints.size());
    for (const auto& c : desc.constraints) {
        h.mix_u8(c.index());
        std::visit([&h](const auto& cs) {
            using T = std::decay_t<decltype(cs)>;
            if constexpr (std::is_same_v<T, LookAtConstraint>) {
                h.mix_vec3(cs.target);
            } else if constexpr (std::is_same_v<T, KeepHorizonConstraint>) {
                // No fields.
            } else if constexpr (std::is_same_v<T, DampedFollowConstraint>) {
                h.mix_f32(cs.damping);
            } else if constexpr (std::is_same_v<T, DistanceConstraint>) {
                h.mix_f32(cs.min_distance);
                h.mix_f32(cs.max_distance);
            } else if constexpr (std::is_same_v<T, RotationLimitConstraint>) {
                h.mix_f32(cs.max_pitch_deg);
                h.mix_f32(cs.max_yaw_deg);
                h.mix_f32(cs.max_roll_deg);
            }
        }, c);
    }

    return h.h;
}

} // namespace chronon3d::camera_v1
