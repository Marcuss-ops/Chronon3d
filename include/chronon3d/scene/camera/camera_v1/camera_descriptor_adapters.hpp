// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp
//
// Pure adapters from supported authoring types to the canonical V1
// descriptor.
//
// Each adapter returns a CameraDescriptor with NO registry lookup, NO mutex,
// and NO scene mutation.
//
// Equivalence rule (1:1): for any SampleTime t, the Camera2_5D produced by
// compile_camera(adapter(...)).evaluate(t) is numerically stable within the
// supported camera tolerance.
// Covered by the canonical compiled camera source tests.
//
// Mapping summary:
//   animation::CameraMotionParams → PoseTracksSource + IdleOscillation
//
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <chronon3d/animations/camera_motion_params.hpp>     // CameraMotionParams

namespace chronon3d::camera_v1 {

// ── Pure adapter: CameraMotionParams → CameraDescriptor ──────────────────
// PoseTracksSource carries the from→to keyframes (2 keys for primary, or
// 1-axis rotation tween for axes-only mode). IdleOscillation modifier is
// appended when p.idle.enabled.  Uses canonical animation helpers from
// <chronon3d/animations/camera_motion_params.hpp>.
[[nodiscard]] CameraDescriptor
camera_descriptor_from(const chronon3d::animation::CameraMotionParams& p);

} // namespace chronon3d::camera_v1
