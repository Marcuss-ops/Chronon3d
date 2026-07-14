// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_descriptor_adapters.cpp
//
// Pure adapters from legacy camera authoring types to CameraDescriptor.
//
// Each adapter is:
//   • Side-effect-free — does not mutate any scene, registry, or cache.
//   • Deterministic — returns the same CameraDescriptor for the same input.
//
// CameraRig is mapped directly to the OrbitMotion source (canonical V1),
// so the V1 runtime evaluates the orbit math natively.  CameraMotionParams
// is mapped to the new CameraMotionParamsSource variant (TICKET-P2-29):
// the V1 runtime evaluates the motion math continuously via
// CameraMotionParamsSource::sample_at() at any frame, eliminating the
// prior 60-sample discrete-time bake (constexpr int n = 60; for (...) bake).
// Mathematically equivalent to the prior bake within ε (61 keyframes via
// linear interpolation between samples).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>

#include <chronon3d/animations/camera_motion_params.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/glm_types.hpp>

// TICKET-P2-29: <algorithm> + <cmath> + <vector> removed — the prior 60-sample
// bake (constexpr int n = 60; for (...) bake) used std::max + std::round
// (from <algorithm> + <cmath>); <vector> was pulled in transitively.  The
// continuous-time evaluation in CameraMotionParamsSource::sample_at() uses
// only inline math + chronon3d::animation::{lerp, easing_value, normalized_time}.

namespace chronon3d::camera_v1 {

namespace {

// Single canonical base fps for the adapters' t=0 snapshot reads.
constexpr FrameRate kAdapterBaseFps{60, 1};

// TICKET-P2-29: local eval_camera_motion_params() REMOVED.  The body is
// now CameraMotionParamsSource::sample_at() (defined just below the
// anonymous namespace).  See TICKET-P2-29 for the migration lineage; the
// 60-sample discrete-time bake (constexpr int n = 60; for (...) bake) is
// gone — the V1 runtime evaluates the motion math natively at any frame
// via sample_at().

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────
// CameraMotionParamsSource::sample_at (TICKET-P2-29)
// ────────────────────────────────────────────────────────────────────────
// Continuous-time evaluation: returns the Camera2_5D pose at the given
// frame per the canonical animation helpers from
// <chronon3d/animations/camera_motion_params.hpp>.  Body was lifted
// verbatim from the (now-removed) local eval_camera_motion_params() free
// function; the only change is `p.X` → `params.X` (now a member field).
Camera2_5D CameraMotionParamsSource::sample_at(Frame ctx_frame) const {
    using namespace chronon3d::animation;

    const Frame local_frame = (ctx_frame >= params.start_frame)
                                  ? (ctx_frame - params.start_frame)
                                  : Frame{0};

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = params.pose.position;
    cam.rotation = params.pose.rotation;
    cam.zoom = params.pose.zoom;

    if (params.primary.enabled && params.primary.duration > 0) {
        const f32 t = easing_value(
            params.primary.easing,
            normalized_time(local_frame, params.primary.duration));
        cam.position = lerp(params.primary.from.position, params.primary.to.position, t);
        cam.rotation = lerp(params.primary.from.rotation, params.primary.to.rotation, t);
        cam.zoom     = lerp(params.primary.from.zoom,        params.primary.to.zoom,        t);
    } else {
        const f32 t = normalized_time(local_frame, params.duration);
        cam.position = params.position;
        cam.zoom = params.zoom;
        switch (params.axis) {
            case MotionAxis::Tilt:
                cam.rotation.x = lerp(params.start_deg, params.end_deg, t); break;
            case MotionAxis::Pan:
                cam.rotation.y = lerp(params.start_deg, params.end_deg, t); break;
            case MotionAxis::Roll:
                cam.rotation.z = lerp(params.start_deg, params.end_deg, t); break;
        }
    }
    // Idle is applied as an IdleOscillation modifier at the descriptor level,
    // not baked into the source keys.
    return cam;
}

// ───────────────────────────────────────────────────────────────────────────
// Adapter 1: CameraMotionParams → CameraDescriptor
// ───────────────────────────────────────────────────────────────────────────
CameraDescriptor
camera_descriptor_from(const chronon3d::animation::CameraMotionParams& p) {
    CameraDescriptor d;
    d.id = "adapter_camera_motion_params";

    // TICKET-P2-29: the 60-sample discrete-time bake (constexpr int n = 60;
    // for (...) bake) is GONE.  CameraMotionParams is now held verbatim in
    // the new CameraMotionParamsSource variant; the V1 runtime evaluates
    // sample_at() at any frame (no 61-keyframe discretization, no
    // constexpr n = 60 constant, no std::round call).

    if (p.idle.enabled) {
        IdleOscillation idle;
        idle.position_amplitude       = p.idle.position_amplitude;
        idle.rotation_amplitude_deg   = p.idle.rotation_amplitude_deg;
        idle.zoom_amplitude           = p.idle.zoom_amplitude;
        idle.frequency_hz             = p.idle.frequency_hz;
        idle.phase                    = p.idle.phase_offset;
        d.modifiers.push_back(idle);
    }

    // TICKET-P2-29: continuous-time source.  The CameraMotionParams struct is
    // embedded verbatim; sample_at() is called by CameraProgram::evaluate()
    // (see camera_program.cpp: get_if<CameraMotionParamsSource> branch).
    d.source = CameraMotionParamsSource{p};
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
// Adapter 2: CameraRig (modern) → CameraDescriptor with OrbitMotion source
// ───────────────────────────────────────────────────────────────────────────
CameraDescriptor
camera_descriptor_from(const chronon3d::CameraRig& rig) {
    CameraDescriptor d;
    d.id = rig.name.empty() ? std::string{"adapter_camera_rig"}
                            : std::string{"adapter_"} + rig.name;

    // Direct orbit mapping — canonical V1 form.  The V1 runtime's
    // eval_orbit_motion() evaluates the orbit math natively.
    OrbitMotion orb;
    orb.target = rig.target;
    orb.yaw    = AnimatedValue<float>(rig.orbit_yaw);
    orb.pitch  = AnimatedValue<float>(rig.orbit_pitch);
    orb.radius = AnimatedValue<float>(rig.orbit_radius);
    orb.track  = rig.track;
    orb.dolly  = AnimatedValue<float>(rig.dolly);
    orb.roll   = AnimatedValue<float>(rig.roll);

    d.source      = orb;
    d.orientation = FixedOrientation{};

    // Base state — non-motion channels live in CameraBaseSpec.
    d.base.enabled     = true;
    d.base.parent_name = rig.parent_name;
    d.base.rotation    = Vec3{0.0f, 0.0f, 0.0f};  // orbit math owns rotation
    d.base.orientation = Quat{1.0f, 0.0f, 0.0f, 0.0f};

    // Lens model — copy from the rig's DOF physical-model fields.
    const SampleTime kBase = SampleTime::from_frame(0.0, kAdapterBaseFps);
    d.base.lens.focal_length   = rig.dof.focal_length.evaluate(kBase);
    d.base.lens.sensor_width   = rig.dof.sensor_width.evaluate(kBase);
    d.base.lens.sensor_height  = rig.dof.sensor_height.evaluate(kBase);
    d.base.lens.f_stop         = rig.dof.f_stop.evaluate(kBase);
    d.base.lens.close_focus    = rig.dof.close_focus.evaluate(kBase);
    d.base.lens.gate_fit       = rig.dof.gate_fit;

    // DoF settings — copy from the rig's DOF block.
    d.base.dof.enabled            = rig.dof.enabled;
    d.base.dof.use_physical_model = rig.dof.use_physical_model;
    d.base.dof.aperture           = rig.dof.aperture.evaluate(kBase);
    d.base.dof.max_blur           = rig.dof.max_blur.evaluate(kBase);
    d.base.dof.focus_distance     = rig.dof.focus_distance.evaluate(kBase);
    d.base.dof.focus_z            = rig.dof.focus_z.evaluate(kBase);

    // Motion blur (TICKET-026 — `enabled` removed; mode is the canonical
    // "active?" indicator).
    d.base.motion_blur.mode              = rig.motion_blur.mode;
    d.base.motion_blur.samples           = rig.motion_blur.samples;
    d.base.motion_blur.shutter_angle_deg = rig.motion_blur.shutter_angle_deg;
    d.base.motion_blur.shutter_phase_deg = rig.motion_blur.shutter_phase_deg;
    d.base.motion_blur.pattern           = rig.motion_blur.pattern;
    d.base.motion_blur.filter            = rig.motion_blur.filter;
    d.base.motion_blur.jitter_seed       = rig.motion_blur.jitter_seed;

    // LookAt target — convert target_name to a layer-lookat when the rig
    // is in TwoNode mode AND target_name is non-empty.
    if (rig.mode == chronon3d::CameraRigMode::TwoNode && !rig.target_name.empty()) {
        d.orientation = LookAtLayer{rig.target_name};
        d.base.point_of_interest_enabled = true;
    }

    return d;
}

// ───────────────────────────────────────────────────────────────────────────
// Adapter 3: legacy slim Camera → CameraDescriptor  (P3-E / TICKET-034F+)
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
