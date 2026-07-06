#pragma once
// ==============================================================================
// tests/visual/camera_truth/camera_truth_orbit.hpp
//
// CameraTruthOrbit — 3D camera orbit projection truth scene.
//
// Same 4 cards as CameraTruthTest (behind/near/mid/far) + origin crosshair,
// but with an OrbitMotion camera that orbits around the origin (yaw 0→90°,
// radius 1000).  Uses the CameraDescriptor API (camera_v1::OrbitMotion)
// to test the compiled orbit evaluation path end-to-end.
//
// Expected visual behaviour:
//   - At yaw=0:   camera directly behind origin, cards spread on screen
//   - At yaw=90°: camera 90° to the right, cards shifted left on screen
//   - Near card moves the most (orbital parallax)
//   - Far card stays most stable
//   - Behind card (z=-1200) always clipped (depth < 0 from orbit perspective)
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::test {

Composition make_camera_truth_orbit();

} // namespace chronon3d::test
