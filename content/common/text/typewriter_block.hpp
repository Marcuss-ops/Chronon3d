#pragma once

// ── typewriter_block — 2-line per-glyph typewriter reveal ────────────────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 4 of 4).  Single-responsibility: the shared 2-line typewriter block
// (used by both the production compositions and the A/B test in
// tests/visual/glow_ab/).
//
// DEPENDENCIES: text_reveal (for build_text_reveal_line + TextRevealDescriptor
// + font_regular) + glyph_layout (for measure_text_width).
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 12 existing callers' `using`
// declarations).

#include <chronon3d/core/types/types.hpp>          // f32, Color (canonical SDK types header)
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::text_reveal {

// build_2line_typewriter — shared 2-line per-glyph typewriter block.
//
// Builds a centered 2-line typewriter reveal.  Both lines share the same
// left edge and use the production defaults (Poppins-Regular, 4 px tracking,
// -50 px base-y, production text/shadow colors) unless overridden.
//
// Moved here from content/examples/text/text_animations.cpp so the A/B
// test in tests/visual/glow_ab/ and the production compositions share
// exactly one definition.
//
// Default-args are all set per the pre-split API; per AGENTS.md `### C++
// default-arg uniqueness per TU` they live ONLY in the .hpp declaration.
void build_2line_typewriter(SceneBuilder& s,
                             const std::string& line1, f32 size1,
                             const std::string& line2, f32 size2,
                             f32 start_delay_2 = 36.0f,
                             f32 line_spacing = 85.0f,
                             bool slide_up = false,
                             f32 glow_intensity = 0.0f,
                             f32 base_y = -50.0f,
                             f32 tracking = 4.0f,
                             Color text_color = {0.92f, 0.93f, 0.97f, 1.0f},
                             Color shadow_color = {0.0f, 0.0f, 0.0f, 0.15f});

} // namespace chronon3d::content::text_reveal
