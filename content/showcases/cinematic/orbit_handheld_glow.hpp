// ═══════════════════════════════════════════════════════════════════════════
//  orbit_handheld_glow.hpp — Phase-2.3 split: 1 composition per file.
//
//  Source-of-truth factory declaration for the OrbitHandheldGlow
//  cinematic composition (slow closed Catmull-Rom orbit + wiggle3D
//  handheld shake + over-exposed neon halo bloom-in).  Implementation
//  lives in orbit_handheld_glow.cpp at the same path.  The canonical
//  umbrella header cinematic_text_camera.hpp re-exports this
//  declaration for backward compatibility with consumers that
//  previously reached it through `content/anims/compositions/
//  cinematic_text_camera.hpp`.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// 3. OrbitHandheldGlow — slow Catmull-Rom orbit + wiggle3D shake.
Composition orbit_handheld_glow();

} // namespace chronon3d::content::anims
