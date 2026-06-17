#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// ── 5 Easy Animations (single text layer) ─────────────────────────────
Composition anim_slide_up();
Composition anim_scale_pop();
Composition anim_blur_focus();
Composition anim_slide_left();
Composition anim_bounce_drop();

// ── 5 Typewriter Animations (per-glyph reveal) ────────────────────────
Composition anim_typewriter_simple();
Composition anim_typewriter_cursor();
Composition anim_typewriter_slide();
Composition anim_typewriter_glow();
Composition anim_typewriter_stagger();

} // namespace chronon3d::content::anims
