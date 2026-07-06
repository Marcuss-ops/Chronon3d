#pragma once
// ==============================================================================
// tests/visual/camera_truth/camera_truth_test.hpp
//
// CameraTruthTest — minimal 3D camera projection truth scene.
//
// Three colored rectangles at different depths (z=0, 500, 1000) with
// a TwoNode camera that pans left/right across frames 0/30/60.
// Designed to answer one question: does the render path actually use
// the 3D camera to project world-space coordinates to screen?
//
// Expected visual behaviour:
//   - The closest card (z=0) moves the most when camera pans (parallax)
//   - The farthest card (z=1000) stays most stable
//   - All cards are visible and projected to different screen positions
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::test {

Composition make_camera_truth_test();

} // namespace chronon3d::test
