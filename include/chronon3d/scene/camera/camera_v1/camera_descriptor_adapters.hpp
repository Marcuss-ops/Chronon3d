// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp
//
// PR1 of the camera-API unification refactor: pure adapters from the legacy
// authoring types to the canonical V1 descriptor.
//
// Each adapter returns a CameraDescriptor with NO registry lookup, NO mutex,
// NO scene mutation. The legacy path remains in place for safety; it will
// be re-implemented as a thin wrapper around these adapters in a later PR.
//
// Equivalence rule (1:1): for any SampleTime t, the Camera2_5D produced by
// running the legacy pipeline at t is *numerically* equal (within epsilon)
// to the Camera2_5D produced by compile_camera(adapter(...)).evaluate(t).
// See test_camera_descriptor_adapters.cpp for the proof.
//
// Mapping summary:
//   animation::CameraMotionParams → PoseTracksSource + IdleOscillation
//   CameraRig (modern)             → PoseTracksSource baked at N samples *
//                                            (faithful regression via keyframes)
//   CameraShotProfile              → delegates to the rig adapter.
//
// * The rig adapter BAKES the legacy CameraRig into an N-sample keyframe set
//   on a PoseTracksSource.   This is intentionally agnostic to the descriptor
//   runtime's orbit math — the equivalence test passes at any SampleTime via
//   the descriptor's own interpolation, regardless of how the V1 runtime's
//   OrbitMotion variant produces values.  This means we can land this PR
//   without depending on a future fix to V1's orbit Z-sign convention.
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <chronon3d/animations/camera_motion_params.hpp>     // CameraMotionParams
#include <chronon3d/scene/model/camera/camera.hpp>           // Camera (slim legacy struct)
#include <chronon3d/scene/model/camera/camera_rig.hpp>        // CameraRig
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp> // CameraShotProfile

#include <cstdint>

namespace chronon3d::camera_v1 {

// ── RigBakeDensity — number of uniformly-spaced samples used to bake a rig
// into a PoseTracksSource.  Dense gives near-exact linear-interpolation
// equivalence at 1:100 benchmark rate; Default is fine for normal cases.
// Sparse is intended for visual review / debugging.
enum class RigBakeDensity : std::int32_t {
    Sparse  = 16,
    Default = 60,
    Dense   = 240,
};

// ── Pure adapter: CameraMotionParams → CameraDescriptor ──────────────────
// PoseTracksSource carries the from→to keyframes (2 keys for primary, or
// 1-axis rotation tween for axes-only mode). IdleOscillation modifier is
// appended when p.idle.enabled.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::animation::CameraMotionParams& p);

// ── Pure adapter: CameraRig (modern) → CameraDescriptor ───────────────────
// Bakes the rig into a PoseTracksSource sampled at N=`density` evenly-
// distributed SampleTime points across [0, total_samples seconds]. DOF and
// motion blur are copied from the rig's static fields.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::CameraRig& rig,
                       RigBakeDensity density = RigBakeDensity::Default);

// ── Pure adapter: CameraShotProfile → CameraDescriptor ────────────────────
// Delegates to the rig adapter.  Validator / framing / auto_fit / emit_report
// are post-processing concerns and are NOT carried into the descriptor.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::CameraShotProfile& shot,
                       RigBakeDensity density = RigBakeDensity::Default);

// ── Pure adapter: legacy slim Camera → CameraDescriptor ──────────────────
//
// Bridges the OPP's flat `chronon3d::Camera` struct (the field that was just
// marked `[[deprecated]]` on `chronon3d::Composition`) into the canonical
// V1 descriptor form.  The legacy struct carries only:
//
//   * transform.position + transform.rotation
//   * fov_deg (perspective vertical FOV)
//   * near_plane, far_plane            (used to seed a default PhysicalLens)
//
// — no parent / target / DoF / motion-blur / zoom slider.  The adapter fills
// the V1 descriptor's `base` from these and leaves `source =
// StaticCameraSource{}` so the snapshot is exactly what `comp.camera` was
// representing at the time of the call.  Mutating the legacy `Camera`
// afterwards does NOT re-trigger the descriptor; this matches the static
// snapshot semantics of the slim legacy field.
//
// READ-ONLY CONTRACT: this adapter is the canonical one-way bridge into
// the V1 pipeline.  The renderer MUST stop reading the legacy
// `Composition::camera` field directly; callers wanting V2 camera input
// should be reading `CompiledComposition.camera_program->evaluate(...)`.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::Camera& legacy_cam);

} // namespace chronon3d::camera_v1
