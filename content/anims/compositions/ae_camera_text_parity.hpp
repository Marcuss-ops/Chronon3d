#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// AECameraTextParity
//
// 360-frame (12-second @ 30 fps) cinematic composition that exercises the
// full Chronon3D camera stack against the classical After Effects Classic 3D
// feature set.  Split into 6 segments (60 frames each):
//
//   0–59   : Static baseline                (PhysicalLens 50mm / f/2.8 / no DOF)
//   60–119 : Dolly-Zoom (Hitchcock)         (radius 1200→800, zoom 1000→1500)
//   120–179: TwoNode orbit                  (yaw ±35°, pitch ±15°, radius 1200)
//   180–239: Rack focus                     (physical DOF, focus NEAR→FOCUS→FAR)
//   240–299: Whip pan + motion blur         (16 samples, Stratified, Gaussian)
//   300–359: Stress (roll 0→30°, focal 24→135mm, target crossing Y)
//
// Render the full 360-frame MP4:
//   chronon3d_cli video AECameraTextParity -o output/ae_camera_text_parity.mp4
//
// Render a single diagnostic frame:
//   chronon3d_cli render AECameraTextParity --frames 0   -o output/parity_t000.png
//   chronon3d_cli render AECameraTextParity --frames 90  -o output/parity_t090.png
//   chronon3d_cli render AECameraTextParity --frames 180 -o output/parity_t180.png
//   chronon3d_cli render AECameraTextParity --frames 270 -o output/parity_t270.png
//   chronon3d_cli render AECameraTextParity --frames 350 -o output/parity_t350.png
//
// The composition is structurally identical to the proposed After Effects
// scene (1920×1080, 30 fps, 5 stacked text layers + grid + cross + 3 cards
// + camera_target + focus_target) so AE renders of the same scene can be
// compared frame-for-frame under the acceptance metrics listed in
// `docs/CAMERA_AE_GAP_VENDETTA.md`.
//
// The composition is intentionally framework-light: it does NOT use
// camera_test_orchestrator (which assumes single-segment validation
// reports).  Instead it builds the Scene every frame directly so the
// per-segment camera state stays inspectable.  Run `camera-pan-validator`
// / `text-audit` separately to consume the rendered PNG/PNG sequence for
// numerical comparison.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

Composition ae_camera_text_parity();

} // namespace chronon3d::content::anims
