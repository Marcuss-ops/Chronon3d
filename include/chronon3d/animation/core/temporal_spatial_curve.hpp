#pragma once

// ============================================================================
// temporal_spatial_curve.hpp — Umbrella header for temporal + spatial curves
//
// FASE 19 split (post-FASE-18 housekeeping): this header was monolithic
// (~548 lines) and held both time-axis and space-axis machinery.  The
// type declarations were extracted into:
//   - temporal_curve_1d.hpp    (PathTimingMode + TemporalCurve1D class)
//   - spatial_motion_path.hpp  (CubicBezier3D + SpatialKeyframe3D +
//                               MotionSegment3D + MotionPath3D +
//                               MotionPathSampler3D)
//
// This umbrella header now just forwards the two split headers at FILE
// SCOPE (NOT inside any namespace) to preserve all 5 existing call sites
// without API churn:
//   - tests/core/animation/test_temporal_spatial_curve.cpp
//   - tests/visual/cinematic_motion/cinematic_motion_tests.cpp
//   - tests/visual/cinematic_motion/cinematic_motion_scenes_bezier.cpp
//   - tests/visual/cinematic_motion/cinematic_motion_scenes.hpp
//   - tests/visual/cinematic_motion/cinematic_motion_scenes_quat.cpp
//
// The Camera-integration wiring (consuming a MotionPath3D inside a
// CameraTrajectory) lives in `scene/camera/camera_v1` and is tracked on
// the camera-plan roadmap (see FU-1.5).
//
// Behaviour: bit-identical to pre-split.  No public API change.
// ============================================================================

#include <chronon3d/animation/core/temporal_curve_1d.hpp>
#include <chronon3d/animation/core/spatial_motion_path.hpp>

namespace chronon3d {

// All declared types now live in the two split headers above; this
// namespace block is intentionally empty (umbrella forwarding only).

} // namespace chronon3d
