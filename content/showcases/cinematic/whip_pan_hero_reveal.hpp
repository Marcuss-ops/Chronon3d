// ═══════════════════════════════════════════════════════════════════════════
//  whip_pan_hero_reveal.hpp — Phase-2.3 split: 1 composition per file.
//
//  Source-of-truth factory declaration for the WhipPanHeroReveal
//  cinematic composition (violent whip-pan camera snap + stagger
//  typewriter text reveal).  Implementation lives in
//  whip_pan_hero_reveal.cpp at the same path.  The canonical umbrella
//  header cinematic_text_camera.hpp re-exports this declaration for
//  backward compatibility with consumers that previously reached it
//  through `content/anims/compositions/cinematic_text_camera.hpp`.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// 2. WhipPanHeroReveal — violent camera snap + stagger typewriter.
Composition whip_pan_hero_reveal();

} // namespace chronon3d::content::anims
