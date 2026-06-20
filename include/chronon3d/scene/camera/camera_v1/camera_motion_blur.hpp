#pragma once
// ==============================================================================
// chronon3d/include/chronon3d/scene/camera/camera_v1/camera_motion_blur.hpp
//
// PR1 — Backward-compatibility shim.
//
// Prior to PR1 this header declared `class CameraMotionBlurIntegrator`.
// PR1 renames the class to `ShutterPoseSampler` and moves its full
// implementation under `camera_v1/internal/` (an internal helper used by
// CameraProgram / CameraDescriptor, not a public API).
//
// For ABI/source compatibility with 1.x callers (including
// tests/scene/camera/test_camera_motion_blur.cpp and any downstream
// consumers), this header now re-exports:
//
//   chronon3d::camera_v1::CameraMotionBlurIntegrator
//       = chronon3d::camera_v1::internal::ShutterPoseSampler
//   chronon3d::camera_v1::CameraEvaluatorFn
//       = chronon3d::camera_v1::internal::CameraEvaluatorFn
//
// New code should include `<chronon3d/scene/camera/camera_v1/internal/
// shutter_pose_sampler.hpp>` directly instead of this shim.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/internal/shutter_pose_sampler.hpp>

namespace chronon3d::camera_v1 {

/// Backward-compat alias.  New code should prefer `ShutterPoseSampler`.
using CameraMotionBlurIntegrator = ::chronon3d::camera_v1::internal::ShutterPoseSampler;

/// Backward-compat alias.
using CameraEvaluatorFn = ::chronon3d::camera_v1::internal::CameraEvaluatorFn;

} // namespace chronon3d::camera_v1
