// ═══════════════════════════════════════════════════════════════════════════
//  deep_parallax_cascade.hpp — PhASE-2.3 split: 1 composition per file.
//
//  Source-of-truth factory declaration for the DeepParallaxCascade
//  cinematic composition (Catmull-Rom Z-push through floating text).
//  Implementation lives in deep_parallax_cascade.cpp at the same
//  path; the canonical umbrella header cinematic_text_camera.hpp
//  re-exports this declaration for backward compatibility with
//  every consumer that previously reached it through
//  `content/anims/compositions/cinematic_text_camera.hpp`.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// 1. DeepParallaxCascade — Catmull-Rom Z-push through floating text.
Composition deep_parallax_cascade();

} // namespace chronon3d::content::anims
