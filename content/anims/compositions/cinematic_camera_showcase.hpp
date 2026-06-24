#pragma once
// ────────────────────────────────────────────────────────────────────────────
// content/anims/compositions/cinematic_camera_showcase.hpp
//
// CinematicCameraShowcase — single composition that exercises the
// Agent-1 "real-camera" Bezier TrajectoryMotion pipeline alongside the
// four canonical text reveal behaviors from
// `::chronon3d::registry::make_default_text_preset_registry()`:
//
//   1. cinematic_title_reveal — hero title (CHRONON3D)
//   2. word_cascade           — closing subtitle (MOTION BUILT HERE)
//   3. tracking_close         — continuous idle label (v1.0)
//   4. character_cascade      — secondary per-glyph label (STUDIO CHRONON)
//
// DEFINITION OF DONE (Agente 3 spec):
//   ✓ 1920x1080 @ 30 fps, 180 frame composition
//   ✓ At least 3 depth planes (foreground Z=+700, mid Z=+200, hero Z=0,
//                       subtitle Z=-400, far Z=-1500)
//   ✓ Camera rides a Bezier curve with REAL handles (P0→P1→P1.5→P2 + hold)
//   ✓ Four distinct text-presentation behaviors (one per preset above)
//   ✓ Motion blur visible during the curve phase (frames 30-70 + 110-145)
//   ✓ DOF progressive focus 1800→0 across hero arrival then static
//   ✓ Banking visible at 0/+15/0 across Phases A/B/C only (keep_horizon at E)
//   ✓ Final hold E (frames 145-180 = 35 frame ≥ 15) with roll = 0
//   ✓ Renders via `chronon3d_cli` (see tools/render_cinematic_showcase.sh)
//
// ANTI-DUPLICATION (AGENTS.md / docs/ANTI_DUPLICATION_RULES.md / P1):
//   * Camera core (camera_v1::CameraProgram / OrientAlongPath /
//     TrajectoryMotion evaluator) is NOT duplicated here. The composition
//     reads `trajectory.sample(ctx)` and pushes the result into
//     `s.camera().set(cam)` — only the *application* step lives here.
//   * Text animator is NOT duplicated. Each "preset behavior" is
//     re-materialized locally via the same LayerBuilder primitives the
//     registered factory bodies use (`scale_drop`, `soft_pop`,
//     `word_stagger`, `tracking_breathing`, `fade_in`). No new registry
//     entries are introduced — the original 24-entry registry is left
//     untouched (per AGENTS.md "Non modificare ... text animator").
//   * Composition registration stays single-source-of-truth via
//     `animation_compositions.cpp::register_anim_compositions`.
//
// ────────────────────────────────────────────────────────────────────────────

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// CinematicCameraShowcase — see top-of-file comment for full DoD.
// Registered in `animation_compositions.cpp::register_anim_compositions`
// under the canonical id "CinematicCameraShowcase"; appears in
// `chronon3d_cli list`.
Composition cinematic_camera_showcase();

} // namespace chronon3d::content::anims
