// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/legacy_camera_adapters.cpp
//
// Bodies for the 3 NEW adapter free functions in legacy_camera_adapters.hpp.
//
// Pure functions: each adapter is deterministic, side-effect-free, and
// returns a fully-populated CameraDescriptor by value.  No mutex, no
// registry lookup, no scene mutation.  See legacy_camera_adapters.hpp
// for the bridge contract + mapping tables.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/legacy_camera_adapters.hpp>

#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <string>

namespace chronon3d::camera_v1 {

namespace {
// Single canonical base fps for the adapters' t=0 snapshot reads.
// Animation-free base state reads (lens, dof numeric fields) are fps-
// agnostic (SampleTime::from_frame(0, F) is always the 0-second mark
// regardless of F), but lifting this constant to a single namespace-
// scoped definition keeps the two adapters from drifting to different
// fps literals (which would be a confusing audit signal even though
// the runtime value is identical).
constexpr FrameRate kAdapterBaseFps{60, 1};
} // anonymous namespace

// =========================================================================
// Adapter 1 — CameraRig (modern) → CameraDescriptor with OrbitMotion source
// =========================================================================
CameraDescriptor
camera_descriptor_from_orbit_rig(const CameraRig& rig) {
    CameraDescriptor d;
    d.id = rig.name.empty()
               ? std::string{"adapter_orbit_rig"}
               : std::string{"adapter_orbit_rig_\""} + rig.name + "\"";

    // OrbitMotion variant — direct orbit channel map.  Unlike
    // camera_descriptor_from(CameraRig, RigBakeDensity) (in
    // camera_descriptor_adapters.cpp) which BAKES the same orbit math
    // into 60 PoseTracksSource keyframes to side-step sign-convention
    // uncertainty, this adapter USES the V1 runtime's native orbit
    // evaluator — no sampling loss, no bake-load cost.
    //
    // Belt-and-braces: AnimatedValue<float> is constructed from the rig's
    // f32-typed AnimatedValue explicitly so the conversion survives any
    // future redefinition of `f32` (defensive against a downstream
    // `using f32 = double` refactor — unlikely but the cast is free).
    OrbitMotion orb;
    orb.target = rig.target;
    orb.yaw    = AnimatedValue<float>(rig.orbit_yaw);
    orb.pitch  = AnimatedValue<float>(rig.orbit_pitch);
    orb.radius = AnimatedValue<float>(rig.orbit_radius);
    orb.track  = rig.track;
    orb.dolly  = AnimatedValue<float>(rig.dolly);
    orb.roll   = AnimatedValue<float>(rig.roll);

    d.source       = orb;
    d.orientation  = FixedOrientation{};

    // Base state — non-motion channels live in CameraBaseSpec.
    d.base.enabled  = true;
    d.base.parent_name              = rig.parent_name;

    // The modern CameraRig doesn't carry a position keyframe (the orbit
    // math produces it from target+radius+yaw+pitch); when the rig has
    // a parent + target, keep base.position at the orbital start so the
    // evaluate()-time orbit math has a sensible seed.
    d.base.rotation.x = 0.0f;  // orbit math owns rotation
    d.base.rotation.y = 0.0f;
    d.base.rotation.z = 0.0f;
    d.base.orientation = Quat{1.0f, 0.0f, 0.0f, 0.0f};  // identity (orbit math fills it)

    // Lens model — copy from the rig's DOF physical-model fields
    // (the rig carries them in CameraRigDOF so it can animate them;
    // V1 CameraBaseSpec::lens is a flat struct, so we use the rig's
    // t=0 frame as the canonical static value).
    const SampleTime kBase   = SampleTime::from_frame(0.0, kAdapterBaseFps);
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
    // "active?" indicator).  CameraRigMotionBlur is the rig-side struct,
    // MotionBlurSettings is the descriptor-side struct.
    d.base.motion_blur.mode              = rig.motion_blur.mode;
    d.base.motion_blur.samples           = rig.motion_blur.samples;
    d.base.motion_blur.shutter_angle_deg = rig.motion_blur.shutter_angle_deg;
    d.base.motion_blur.shutter_phase_deg = rig.motion_blur.shutter_phase_deg;
    d.base.motion_blur.pattern           = rig.motion_blur.pattern;
    d.base.motion_blur.filter            = rig.motion_blur.filter;
    d.base.motion_blur.jitter_seed       = rig.motion_blur.jitter_seed;

    // LookAt target — convert target_name to a layer-lookat when the rig
    // is in TwoNode mode AND target_name is non-empty.  In OneNode mode,
    // the orbit math does NOT have a look-at target (the camera orbits
    // around `target`) so the orientation stays FixedOrientation and the
    // orbit math drives the camera orientation implicitly via the basis
    // construction in OrbitMotion::evaluate().
    if (rig.mode == CameraRigMode::TwoNode && !rig.target_name.empty()) {
        d.orientation = LookAtLayer{rig.target_name};
    }
    return d;
}

// =========================================================================
// Adapter 2 — AnimatedCamera2_5D → CameraDescriptor with PoseTracksSource
// =========================================================================
CameraDescriptor
camera_descriptor_from_animated(const AnimatedCamera2_5D& cam) {
    CameraDescriptor d;
    d.id           = "adapter_animated_camera_2_5d";
    d.orientation  = FixedOrientation{};

    // PoseTracksSource — direct AnimatedValue field transfer.  Both
    // AnimatedCamera2_5D and PoseTracksSource use the same
    // `AnimatedValue<T>` carrier, so the keyframes are bit-exact across
    // the bridge: the legacy's `evaluate(t)` was `keyframe.interpolate(t)`
    // and the V1's PoseTracksSource evaluation does the same.
    PoseTracksSource pts;
    pts.position      = cam.position;
    pts.rotation      = cam.rotation;
    pts.target        = cam.point_of_interest;
    pts.use_target    = cam.point_of_interest_enabled;
    pts.zoom          = cam.zoom;
    pts.fov_deg       = cam.fov_deg;
    pts.focus_distance = cam.focus_distance;
    pts.aperture      = cam.aperture;
    pts.max_blur      = cam.max_blur;

    d.source = pts;

    // Base state — lens model + DoF block copied verbatim (AnimatedCamera2_5D
    // carries both as flat fields; V1 expects them on CameraBaseSpec).
    d.base.enabled = cam.enabled;
    d.base.lens.focal_length  = cam.focal_length.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.lens.sensor_width  = cam.sensor_width.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.lens.sensor_height = cam.sensor_height.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.lens.f_stop        = cam.f_stop.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.lens.close_focus   = cam.close_focus.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.lens.gate_fit      = cam.gate_fit;
    d.base.dof.enabled            = cam.dof_enabled;
    d.base.dof.use_physical_model = cam.use_physical_model;
    // DoF numeric fields on d.base.dof stay at struct defaults (the legacy
    // AnimatedCamera2_5D has no `focus_distance`/`focus_z`/`aperture` carri...
    // wait — it DOES carry focus_z + aperture + max_blur via the animated
    // channels.  Mirror those onto the base so the projection pipeline
    // sees a coherent snapshot when the descriptor is compiled.
    d.base.dof.focus_z    = cam.focus_z.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.dof.aperture   = cam.aperture.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));
    d.base.dof.max_blur   = cam.max_blur.evaluate(SampleTime::from_frame(0.0, kAdapterBaseFps));

    // Motion blur — AnimatedCamera2_5D does NOT surface a MotionBlurSettings
    // block (the legacy struct is older than TICKET-026); leave
    // base.motion_blur at struct defaults (Off) so the projection
    // pipeline reads "no motion blur".

    return d;
}

// =========================================================================
// Adapter 3 — legacy preset function → CameraDescriptor
// =========================================================================
CameraDescriptor
camera_descriptor_from_legacy_preset(
    const std::string& preset_name,
    const std::function<AnimatedCamera2_5D()>& preset_invocation) {
    // Invoke the preset -> AnimatedCamera2_5D. The result is forwarded
    // to Adapter 2 verbatim.  The preset_name is preserved in the
    // descriptor's id so the OPP renderer can log a deterministic
    // per-preset identifier (helpful for the migration trace).
    AnimatedCamera2_5D cam = preset_invocation();
    CameraDescriptor d = camera_descriptor_from_animated(cam);
    if (!preset_name.empty()) {
        d.id = std::string{"adapter_legacy_preset_"} + preset_name;
    }
    return d;
}

} // namespace chronon3d::camera_v1
