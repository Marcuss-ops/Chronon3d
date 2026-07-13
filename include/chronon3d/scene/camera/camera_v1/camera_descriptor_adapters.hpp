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
//   CameraRig (modern)             → OrbitMotion (canonical V1 source)
//   CameraShotProfile              → delegates to the rig adapter.
//
// The CameraRig adapter maps directly to the OrbitMotion variant, letting
// the V1 runtime evaluate the orbit math natively (no bake interpolation
// loss).  See legacy_camera_adapters.hpp for the temporary migration bridge.
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <chronon3d/animations/camera_motion_params.hpp>     // CameraMotionParams
#include <chronon3d/scene/model/camera/camera.hpp>           // Camera (slim legacy struct)
#include <chronon3d/scene/model/camera/camera_rig.hpp>        // CameraRig (modern)

namespace chronon3d::camera_v1 {

// ── Pure adapter: CameraMotionParams → CameraDescriptor ──────────────────
// PoseTracksSource carries the from→to keyframes (2 keys for primary, or
// 1-axis rotation tween for axes-only mode). IdleOscillation modifier is
// appended when p.idle.enabled.  Uses canonical animation helpers from
// <chronon3d/animations/camera_motion_params.hpp>.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::animation::CameraMotionParams& p);

// ── Pure adapter: CameraRig (modern) → CameraDescriptor ───────────────────
// Maps the rig directly to an OrbitMotion source (canonical V1 form).
// The V1 runtime's eval_orbit_motion() evaluates the orbit math natively,
// so there is no bake interpolation loss.  DOF, motion blur and lens are
// copied from the rig's static fields.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::CameraRig& rig);

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
