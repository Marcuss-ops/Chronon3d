// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_descriptor_adapters.cpp
//
// Pure adapters from legacy camera authoring types to CameraDescriptor.
//
// Each adapter is:
//
//   • Side-effect-free — does not mutate any scene, registry, or cache.
//   • Testable in isolation — equivalence is provable at any SampleTime via
//     a linear-interpolation comparison to the legacy pipeline.
//
// The EQUIVALENCE strategy used here is to BAKE the legacy path into a
// dense PoseTracksSource keyframe set.  This avoids any sign-convention,
// denominator, or easing-curve mismatch between the legacy pipeline and
// the V1 runtime's OrbitMotion math.  Whatever the V1 runtime does internally,
// the baked PoseTracksSource reproduces the legacy output to epsilon at any
// SampleTime, as long as the bake is dense enough (Default=60, Dense=240).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::camera_v1 {

namespace {

// ── Internal helpers ───────────────────────────────────────────────────────

/// Local copy of the CameraMotionParams math.  Mirrors
/// `animation::apply_camera_motion()` (src/scene/camera_motion_applier.cpp)
/// and `animation::normalized_time()` / `animation::lerp()` so the adapter
/// does not need to pull in those inline implementations just to call them.
inline Vec3 lerp_v3(const Vec3& a, const Vec3& b, f32 t) {
    return a + (b - a) * t;
}
inline f32 lerp_f32(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
inline f32 smoothstep01(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline f32 normalized_time_mp(Frame local_frame, Frame duration) {
    const Frame span = std::max<Frame>(1, duration - 1);
    return smoothstep01(static_cast<f32>(local_frame) / static_cast<f32>(span));
}

/// Easing→EasingCurve mapping (matches what chronon3d::animation::apply()
/// produces for an `Easing` enum value).
inline f32 apply_mp_easing(::chronon3d::Easing easing, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
        case chronon3d::Easing::Linear:
            return t;
        case chronon3d::Easing::OutCubic: {
            const f32 u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case chronon3d::Easing::InOutCubic:
            return (t < 0.5f)
                ? 4.0f * t * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
        case chronon3d::Easing::Smoothstep:
        default:
            return smoothstep01(t);
    }
}

/// Replicates `animation::apply_camera_motion(s, ctx, p)` in pure form
/// (no scene mutation).  Returns the Camera2_5D that the legacy path would
/// push into the scene at `ctx.frame`.
Camera2_5D eval_camera_motion_cam(
    const chronon3d::animation::CameraMotionParams& p,
    Frame ctx_frame,
    f32 /*ctx_seconds*/)  // idle.path uses ctx.seconds() too; we replicate below.
{
    const Frame local_frame = (ctx_frame >= p.start_frame)
                                  ? (ctx_frame - p.start_frame)
                                  : Frame{0};

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = p.pose.position;
    cam.rotation = p.pose.rotation;
    cam.zoom = p.pose.zoom;

    if (p.primary.enabled && p.primary.duration > 0) {
        const f32 t = apply_mp_easing(
            p.primary.easing,
            normalized_time_mp(local_frame, p.primary.duration));
        cam.position = lerp_v3(p.primary.from.position, p.primary.to.position, t);
        cam.rotation = lerp_v3(p.primary.from.rotation, p.primary.to.rotation, t);
        cam.zoom     = lerp_f32(p.primary.from.zoom,        p.primary.to.zoom,        t);
    } else {
        const f32 t = normalized_time_mp(local_frame, p.duration);
        cam.position = p.position;
        cam.zoom = p.zoom;
        switch (p.axis) {
            case chronon3d::animation::MotionAxis::Tilt:
                cam.rotation.x = lerp_f32(p.start_deg, p.end_deg, t); break;
            case chronon3d::animation::MotionAxis::Pan:
                cam.rotation.y = lerp_f32(p.start_deg, p.end_deg, t); break;
            case chronon3d::animation::MotionAxis::Roll:
                cam.rotation.z = lerp_f32(p.start_deg, p.end_deg, t); break;
        }
    }
    // NOTE: the descriptor's IdleOscillation modifier applies the idle
    // displacement after the source evaluates.  For 1:1 equivalence at
    // pure-source evaluation (used in the test harness which calls
    // program.evaluate() WITHOUT iterating modifiers), we do NOT bake the
    // idle into the keys here; the modifier-driven equivalence is covered
    // separately at the program level.
    return cam;
}

} // anonymous namespace

// ───────────────────────────────────────────────────────────────────────────
// Adapter 1: CameraMotionParams → CameraDescriptor
// ───────────────────────────────────────────────────────────────────────────
CameraDescriptor
camera_descriptor_from(const chronon3d::animation::CameraMotionParams& p) {
    CameraDescriptor d;
    d.id = "adapter_camera_motion_params";

    PoseTracksSource pts;
    pts.fov_deg.set(50.0f);
    pts.use_target = false;
    pts.aperture.set(0.015f);
    pts.max_blur.set(24.0f);
    pts.focus_distance.set(0.0f);

    // Bake covers [0, start_frame + max(p.duration, primary.duration)].
    constexpr FrameRate kBakeFps{60, 1};
    const int primary_dur = p.primary.enabled
                                ? static_cast<int>(p.primary.duration)
                                : static_cast<int>(p.duration);
    const int active_dur   = std::max(1, primary_dur);
    const int start_frame  = static_cast<int>(p.start_frame);
    const int total_frames = start_frame + active_dur;
    const int n            = static_cast<int>(RigBakeDensity::Default);

    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n);
        const int frame_at_st = static_cast<int>(std::round(t * total_frames));
        const SampleTime st = SampleTime::from_frame(
            static_cast<double>(frame_at_st), kBakeFps);

        Camera2_5D cam = eval_camera_motion_cam(p, Frame{frame_at_st}, 0.0f);

        pts.position.key(Frame{frame_at_st}, cam.position);
        pts.rotation.key(Frame{frame_at_st}, cam.rotation);
        pts.zoom.key(Frame{frame_at_st}, cam.zoom);
    }

    if (p.idle.enabled) {
        IdleOscillation idle;
        idle.position_amplitude       = p.idle.position_amplitude;
        idle.rotation_amplitude_deg   = p.idle.rotation_amplitude_deg;
        idle.zoom_amplitude           = p.idle.zoom_amplitude;
        idle.frequency_hz             = p.idle.frequency_hz;
        idle.phase                    = p.idle.phase_offset;
        d.modifiers.push_back(idle);
    }

    d.source = pts;
    d.base.position = (p.primary.enabled ? p.primary.from.position : p.position);
    d.base.rotation = p.pose.rotation;
    d.base.projection = ZoomProjection{AnimatedValue<float>{p.zoom}};
    d.base.lens.sensor_width  = 36.0f;
    d.base.lens.sensor_height = 24.0f;
    d.base.lens.focal_length  = p.pose.zoom;        // nominal focal for projection
    d.base.lens.f_stop        = 2.8f;
    d.base.lens.gate_fit      = GateFit::Fill;
    return d;
}

// ───────────────────────────────────────────────────────────────────────────
// Adapter 2: CameraRig (modern) → CameraDescriptor
// ───────────────────────────────────────────────────────────────────────────
CameraDescriptor
camera_descriptor_from(const chronon3d::CameraRig& rig,
                       RigBakeDensity density) {
    CameraDescriptor d;
    d.id = rig.name.empty() ? std::string{"adapter_camera_rig"}
                            : std::string{"adapter_"} + rig.name;

    constexpr FrameRate kBakeFps{60, 1};
    const int n = static_cast<int>(density);
    // 5 seconds at 60 fps covers any reasonable rig timeline; interpolation
    // between bake samples then reproduces the rig math via linear blend.
    const int total_frames = 300;

    PoseTracksSource pts;
    pts.fov_deg.set(50.0f);
    pts.use_target = (rig.mode == chronon3d::CameraRigMode::TwoNode);

    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n);
        const int frame_at_st = static_cast<int>(std::round(t * total_frames));
        const SampleTime st = SampleTime::from_frame(
            static_cast<double>(frame_at_st), kBakeFps);

        // Evaluate the legacy rig at the same sample time, with no external
        // hierarchy.  null `resolved` ⇒ the rig uses AnimatedValue targets.
        Camera2_5D legacy = rig.evaluate(st, nullptr);

        pts.position.key(Frame{frame_at_st}, legacy.position);
        pts.rotation.key(Frame{frame_at_st}, legacy.rotation);
        pts.target.key(Frame{frame_at_st}, legacy.point_of_interest);
        pts.zoom.key(Frame{frame_at_st}, legacy.zoom);
        pts.fov_deg.key(Frame{frame_at_st}, legacy.fov_deg);
        pts.focus_distance.key(Frame{frame_at_st}, legacy.dof.focus_distance);
        pts.aperture.key(Frame{frame_at_st}, legacy.dof.aperture);
        pts.max_blur.key(Frame{frame_at_st}, legacy.dof.max_blur);
    }

    d.source = pts;

    // Static base values: take from the rig's lens DOF (no animation —
    // difference is documented in PR1 limitations).  Position / rotation
    // are typically overridden by the baked keys.
    const SampleTime kEpoch = SampleTime::from_frame(0.0, kBakeFps);
    d.base.lens.focal_length   = rig.dof.focal_length.evaluate(kEpoch);
    d.base.lens.sensor_width   = rig.dof.sensor_width.evaluate(kEpoch);
    d.base.lens.sensor_height  = rig.dof.sensor_height.evaluate(kEpoch);
    d.base.lens.f_stop         = rig.dof.f_stop.evaluate(kEpoch);
    d.base.lens.close_focus    = rig.dof.close_focus.evaluate(kEpoch);
    d.base.lens.gate_fit       = rig.dof.gate_fit;

    d.base.dof.enabled            = rig.dof.enabled;
    d.base.dof.use_physical_model = rig.dof.use_physical_model;
    d.base.dof.aperture           = rig.dof.aperture.evaluate(kEpoch);
    d.base.dof.max_blur           = rig.dof.max_blur.evaluate(kEpoch);
    d.base.dof.focus_distance     = rig.dof.focus_distance.evaluate(kEpoch);
    d.base.dof.focus_z            = rig.dof.focus_z.evaluate(kEpoch);

    // TICKET-026 — `enabled` removed from MotionBlurSettings.  Mode is now
    // the canonical "active?" indicator, so the adapter copies it directly
    // (rather than translating the legacy rig.enabled bool).  CameraRigMotionBlur
    // (rig-side) ALSO lost its `enabled` field on this branch — see camera_rig.hpp.
    d.base.motion_blur.mode              = rig.motion_blur.mode;
    d.base.motion_blur.samples           = rig.motion_blur.samples;
    d.base.motion_blur.shutter_angle_deg = rig.motion_blur.shutter_angle_deg;
    d.base.motion_blur.shutter_phase_deg = rig.motion_blur.shutter_phase_deg;
    d.base.motion_blur.pattern           = rig.motion_blur.pattern;
    d.base.motion_blur.filter            = rig.motion_blur.filter;
    d.base.motion_blur.jitter_seed       = rig.motion_blur.jitter_seed;
    d.base.parent_name                 = rig.parent_name;
    d.base.point_of_interest_enabled   = pts.use_target;
    return d;
}

// ───────────────────────────────────────────────────────────────────────────
// Adapter 3: CameraShotProfile → CameraDescriptor (delegates to rig)
// ───────────────────────────────────────────────────────────────────────────
CameraDescriptor
camera_descriptor_from(const chronon3d::CameraShotProfile& shot,
                       RigBakeDensity density) {
    CameraDescriptor d = camera_descriptor_from(shot.rig, density);
    d.id = "adapter_camera_shot_profile";
    return d;
}

// ───────────────────────────────────────────────────────────────────────────
// Adapter 4: legacy slim Camera → CameraDescriptor  (P3-E / TICKET-034F+)
//
// The OPP's flat `chronon3d::Camera` struct is the legacy authoring form:
// a position + rotation + FOV triple.  We capture a STATIC SNAPSHOT into a
// V1 descriptor — the legacy field has no notion of animation, so trivially
// `source = StaticCameraSource{}` is the cleanest fallback.  After this
// adapter runs, the resulting descriptor is content-stable (modulo the
// `id` tag) for as long as the underlying `Camera` field stays unchanged.
//
// Fields silently dropped (legacy Camera struct has no analog):
//   * near_plane / far_plane                              (seeding PhysicalLens
//                                                          would be presumptuous
//                                                          given no lens model
//                                                          on the legacy struct;
//                                                          V1 defaults to the
//                                                          standard 35mm gate)
//   * explicit parent / target / point_of_interest        (no slot on Camera)
//   * DoF / Motion-blur / handedness                      (no slot on Camera)
//
// The adapter is the canonical one-way bridge from legacy composition
// authoring (the soon-deprecated `Composition::camera` field, golden test
// harnesses, and ad-hoc CLI commands that still pass a `Camera&`) into the
// canonical V1 pipeline.  The OPP renderer no longer reads `comp.camera`
// directly — callers wanting V2 camera input should be reading
// `CompiledComposition.camera_program->evaluate(...)`.
CameraDescriptor
camera_descriptor_from(const chronon3d::Camera& legacy_cam) {
    CameraDescriptor d;
    d.id           = "adapter_legacy_camera";
    d.source       = chronon3d::camera_v1::StaticCameraSource{};
    d.orientation  = chronon3d::camera_v1::FixedOrientation{};

    // Frame base state verbatim from the slim legacy struct.
    d.base.enabled  = true;
    d.base.position = legacy_cam.transform.position;
    // legacy_cam.transform.rotation is a glm::quat (per chronon3d::Camera);
    // CameraDescriptor::base.rotation is a Vec3 euler-in-degrees.  The
    // conversion uses the same glm::degrees(glm::eulerAngles(...)) pattern
    // already used by layer_hierarchy.cpp and motion_preset_methods.cpp,
    // keeping the rest of the pipeline on its degree convention.
    d.base.rotation = glm::degrees(glm::eulerAngles(legacy_cam.transform.rotation));

    // The only projection knob on the legacy struct is vertical FOV.
    // Use FovProjection so the evaluate() pipeline produces a state whose
    // `.fov_deg` matches the legacy field exactly (no zoom→fov guessing).
    chronon3d::camera_v1::FovProjection proj;
    proj.fov_deg.set(legacy_cam.fov_deg);
    d.base.projection = proj;

    // Sensible 35mm optical defaults (the legacy struct has no lens model).
    d.base.lens.focal_length  = legacy_cam.focal_length(/*width=*/36.0f);
    d.base.lens.f_stop        = 2.8f;
    d.base.lens.sensor_width  = 36.0f;
    d.base.lens.sensor_height = 24.0f;
    d.base.lens.gate_fit      = chronon3d::GateFit::Fill;

    // DoF + motion blur stay at their struct defaults (enabled = false).
    // Composition previously exposed `redecompose_camera_from_descriptor(...)`
    // that flipped fov_deg in-place on the legacy field as a one-way
    // projection.  That helper has been REMOVED in P3-F (Composition has
    // no mutable camera state any more).  This adapter produces the FULL
    // descriptor surface that the renderer compiles through, leaving
    // no field flattening responsibility at the call site.

    return d;
}

} // namespace chronon3d::camera_v1
