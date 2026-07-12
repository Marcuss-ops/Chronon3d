#pragma once

// ============================================================================
// content/examples/text/easy_text_animations.hpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — 5 easy text animation factories
// (single text layer, opacity/position/scale/blur motion).
//
// Extracted from the >360-LoC monolithic `content/examples/text/text_animations.cpp`
// (now deleted; replaced by this hpp/cpp pair + typewriter_animations.hpp/cpp +
// text_animation_registration.hpp/cpp).
//
// 5 factories (registered by `register_text_animation_compositions` in
// `text_animation_registration.cpp`):
//   1. anim_slide_up    — slides up from below with fade
//   2. anim_scale_pop   — scales from small with overshoot
//   3. anim_blur_focus  — blurry → sharp via focus_in
//   4. anim_slide_left  — slides in from the left
//   5. anim_bounce_drop — drops from above with bounce
//
// The 5 typewriters (anim_typewriter_simple/cursor/slide/glow/stagger) live in
// the sister file `typewriter_animations.{hpp,cpp}`.
// ============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

Composition anim_slide_up();
Composition anim_scale_pop();
Composition anim_blur_focus();
Composition anim_slide_left();
Composition anim_bounce_drop();

} // namespace chronon3d::content::anims
