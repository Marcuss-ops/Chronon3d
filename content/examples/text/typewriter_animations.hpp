#pragma once

// ============================================================================
// content/examples/text/typewriter_animations.hpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — 5 typewriter animation factories
// (stable per-glyph reveal, all use the `build_2line_typewriter(spec)` helper
// from content/common/text_reveal_helpers.hpp that landed in Step 10).
//
// Extracted from the >360-LoC monolithic `content/examples/text/text_animations.cpp`
// (now deleted; replaced by this hpp/cpp pair + easy_text_animations.hpp/cpp +
// text_animation_registration.hpp/cpp).
//
// 5 factories (registered by `register_text_animation_compositions` in
// `text_animation_registration.cpp`):
//   1. anim_typewriter_simple   — 2-line, chars fade in left→right
//   2. anim_typewriter_cursor   — 2-line + blinking cursor
//   3. anim_typewriter_slide    — 2-line, chars slide up as they appear
//   4. anim_typewriter_glow     — 2-line, micro glow on new chars (uses Step 10
//                                 build_2line_typewriter with glow_intensity)
//   5. anim_typewriter_stagger  — 4-line rhythmic stagger
//
// Note: the user spec says "4 typewriters" but the file has 5.  Stagger is
// kept here (rather than in easy_text_animations.cpp) because it is a
// typewriter-class animation (per-glyph reveal), not a single-layer easy anim.
// The 5th factory is documented in the code-reviewer forward-points as
// TICKET-TYPEWRITER-ANIMATIONS-COUNT-CLARIFY for a future housekeeping chore.
// ============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

Composition anim_typewriter_simple();
Composition anim_typewriter_cursor();
Composition anim_typewriter_slide();
Composition anim_typewriter_glow();
Composition anim_typewriter_stagger();

} // namespace chronon3d::content::anims
