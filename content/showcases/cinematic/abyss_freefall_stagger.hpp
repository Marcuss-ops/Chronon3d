// ═══════════════════════════════════════════════════════════════════════════
//  abyss_freefall_stagger.hpp — Phase-2.3 split: 1 composition per file.
//
//  Source-of-truth factory declaration for the AbyssFreefallStagger
//  cinematic composition (each letter of "LET FALL" drops in Z while
//  the camera rolls slowly to amplify the vertigo).  Implementation
//  lives in abyss_freefall_stagger.cpp at the same path.  The canonical
//  umbrella header cinematic_text_camera.hpp re-exports this
//  declaration for backward compatibility with consumers that
//  previously reached it through `content/anims/compositions/
//  cinematic_text_camera.hpp`.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// 5. AbyssFreefallStagger — letters drop in Z while camera slowly
// rolls.
Composition abyss_freefall_stagger();

} // namespace chronon3d::content::anims
