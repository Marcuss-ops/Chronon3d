#pragma once
// ==============================================================================
// tests/visual/ae_parity/ae_parity_scenes.hpp
//
// AE Parity Camera Visual Scenes — 10 scene factory functions for
// visual comparison Chronon3D ↔ After Effects.
//
// Each scene isolates one AE parity camera category as defined in
// docs/CAMERA_FEATURE_MATRIX.md §2–§6.
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <string>

namespace chronon3d::test {

/// Default AE parity composition dimensions.
inline constexpr int kAeParityWidth  = 960;
inline constexpr int kAeParityHeight = 540;

// ── AE_CAM_01 — Static camera + grid background (projection baseline) ───────
//
// A static camera looking at a grid with a center cross.
// Verifies: projection math tolerance 1e-3, no NaN, no black frame,
//           random access deterministic.
Composition make_ae_cam_01_static_grid();

// ── AE_CAM_02 — Animated zoom vs FOV projection modes ───────────────────────
//
// Camera switches between Zoom and FOV projection over 60 frames.
// Depth cards at z=0, 200, -200 to show perspective distortion.
// Verifies: zoom/FOV projection equivalence within tolerance,
//           visual difference between projection modes.
Composition make_ae_cam_02_zoom_fov();

// ── AE_CAM_03 — Two-node camera with animated point of interest ─────────────
//
// Camera position animates left→right, POI stays on subject card.
// Verifies: two-node camera rig, look-at behavior, position/POI interpolation.
Composition make_ae_cam_03_two_node_poi();

// ── AE_CAM_04 — Camera parented to null layer ───────────────────────────────
//
// Camera parent_name points to a null layer that translates over 60 frames.
// Subject card at world origin.
// Verifies: parent transform propagation, camera inherits parent motion.
Composition make_ae_cam_04_parent_null();

// ── AE_CAM_05 — Orbit motion with yaw sweep ─────────────────────────────────
//
// OrbitMotion yaw sweeps -60° → +60° around a subject card.
// Verifies: orbit local basis, rotation coherence, camera-local dolly.
Composition make_ae_cam_05_orbit();

// ── AE_CAM_06 — Dolly zoom (Hitchcock effect) ───────────────────────────────
//
// Camera dollies backward while zooming in to maintain subject size.
// Background/foreground cards at different depths to show parallax.
// Verifies: dolly + zoom combination, perspective compression.
Composition make_ae_cam_06_dolly_zoom();

// ── AE_CAM_07 — GateFit modes comparison (Overscan vs Stretch) ──────────────
//
// Same 3D scene rendered with GateFit::Overscan and GateFit::Stretch.
// Cards at edges to show viewport distortion differences.
// Verifies: effective_viewport(), focal_x/focal_y ratio per mode.
Composition make_ae_cam_07_gatefit();

// ── AE_CAM_08 — Depth of field with animated focus distance ─────────────────
//
// Three cards at z = -400, 0, +400. Focus distance animates 0→120 frames
// pulling focus from near→mid→far.
// Verifies: DOF blur radius, focus distance interpolation, aperture effect.
Composition make_ae_cam_08_dof();

// ── AE_CAM_09 — Motion blur with fast camera pan ────────────────────────────
//
// Camera pans rapidly left→right across a scene with static cards.
// Motion blur enabled with TemporalAccumulation, 16 samples, 180° shutter.
// Verifies: motion blur streak, deterministic sampling, shutter angle effect.
Composition make_ae_cam_09_motion_blur();

// ── AE_CAM_10 — Near plane clipping ─────────────────────────────────────────
//
// A large card crosses the near plane (z ≈ -999.5, camera at z=-1000).
// Camera looks at the crossing region.
// Verifies: no explosion, no NaN, no degenerate triangles, stable output.
Composition make_ae_cam_10_near_clip();

} // namespace chronon3d::test
