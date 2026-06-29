// ═══════════════════════════════════════════════════════════════════════════
//  rack_focus_title_swap.hpp — Phase-2.3 split: 1 composition per file.
//
//  Source-of-truth factory declaration for the RackFocusTitleSwap
//  cinematic composition (Vertigo dolly-zoom + opposing blur tracks
//  between FRONT and BACK titles).  Implementation lives in
//  rack_focus_title_swap.cpp at the same path.  The canonical umbrella
//  header cinematic_text_camera.hpp re-exports this declaration for
//  backward compatibility with consumers that previously reached it
//  through `content/anims/compositions/cinematic_text_camera.hpp`.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// 4. RackFocusTitleSwap — Vertigo dolly-zoom + opposing blur tracks.
Composition rack_focus_title_swap();

} // namespace chronon3d::content::anims
