// ============================================================================
// content/examples/text/text_animation_registration.cpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — text animation registration impl.
//
// Replaces the 10 inline `registry.add(...)` calls that previously lived in
// `content/animation_compositions.cpp::register_anim_compositions()` for the
// 5 easy animations + 5 typewriters.
//
// All 10 factories are forward-declared in the corresponding hpp files; the
// factories are defined in `easy_text_animations.cpp` and
// `typewriter_animations.cpp`.  No typewriter logic is duplicated here
// (per user spec §17 ripgrep verify; verified at the bottom of the hpp).
// ============================================================================

#include "text_animation_registration.hpp"

#include "easy_text_animations.hpp"
#include "typewriter_animations.hpp"

namespace chronon3d::content::anims {

void register_text_animation_compositions(CompositionRegistry& registry) {
    // ── 5 easy animations (easy_text_animations.cpp) ─────────────────────
    registry.add("AnimSlideUp",     [](const CompositionProps&) { return anim_slide_up(); });
    registry.add("AnimScalePop",    [](const CompositionProps&) { return anim_scale_pop(); });
    registry.add("AnimBlurFocus",   [](const CompositionProps&) { return anim_blur_focus(); });
    registry.add("AnimSlideLeft",   [](const CompositionProps&) { return anim_slide_left(); });
    registry.add("AnimBounceDrop",  [](const CompositionProps&) { return anim_bounce_drop(); });

    // ── 5 typewriters (typewriter_animations.cpp) ─────────────────────────
    registry.add("AnimTypewriterSimple",  [](const CompositionProps&) { return anim_typewriter_simple(); });
    registry.add("AnimTypewriterCursor",  [](const CompositionProps&) { return anim_typewriter_cursor(); });
    registry.add("AnimTypewriterSlide",   [](const CompositionProps&) { return anim_typewriter_slide(); });
    registry.add("AnimTypewriterGlow",    [](const CompositionProps&) { return anim_typewriter_glow(); });
    registry.add("AnimTypewriterStagger", [](const CompositionProps&) { return anim_typewriter_stagger(); });
}

} // namespace chronon3d::content::anims
