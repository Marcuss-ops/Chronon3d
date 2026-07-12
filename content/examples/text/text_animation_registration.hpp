#pragma once

// ============================================================================
// content/examples/text/text_animation_registration.hpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — text animation registration surface.
//
// Extracted from the >360-LoC monolithic `content/examples/text/text_animations.cpp`
// (now deleted) and the inline `registry.add(...)` calls in
// `content/animation_compositions.cpp::register_anim_compositions()`.
//
// Cat-3 minimal-surface: 1 free function, no new SDK symbols.
// ============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d::content::anims {

// Register the 10 text animation compositions into the given registry:
//
//   5 easy animations (easy_text_animations.cpp):
//     1. AnimSlideUp
//     2. AnimScalePop
//     3. AnimBlurFocus
//     4. AnimSlideLeft
//     5. AnimBounceDrop
//
//   5 typewriters (typewriter_animations.cpp):
//     6. AnimTypewriterSimple
//     7. AnimTypewriterCursor
//     8. AnimTypewriterSlide
//     9. AnimTypewriterGlow
//    10. AnimTypewriterStagger
//
// Called by `content/animation_compositions.cpp::register_anim_compositions()`.
// The 10 inline `registry.add(...)` calls that previously lived in that
// function have been REMOVED; this function replaces them.
void register_text_animation_compositions(CompositionRegistry& registry);

} // namespace chronon3d::content::anims
