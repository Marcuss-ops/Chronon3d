#pragma once

// ── text_reveal_helpers (forward-header shim) ────────────────────────────
//
// P1 refactor — this 381-LoC header-only monolith was split into 4
// single-responsibility TUs in `content/common/text/`:
//   - cinematic_glow    (CinematicGlowPreset + apply_cinematic_glow)
//   - glyph_layout      (GlyphPos + measure_text_width + layout_glyphs)
//   - text_reveal       (TextRevealDescriptor + build_text_reveal_line
//                        + font_bold + font_regular)
//   - typewriter_block  (build_2line_typewriter)
//
// This shim file preserves the EXISTING `#include "content/common/text_reveal_helpers.hpp"`
// call sites for the 12 currently-using TUs:
//   - content/showcases/cinematic/{rack_focus_title_swap,
//     abyss_freefall_stagger, orbit_handheld_glow, deep_parallax_cascade,
//     whip_pan_hero_reveal}.cpp
//   - content/examples/text/typewriter_animations.cpp
//   - content/compositions/chronon_glow_final.cpp
//   - tests/visual/glow_ab/glow_ab_compositions.cpp
//   - tests/certification/test_cert_text_invariants.cpp
//
// CAT-3 / cat-5 2-doc deferred obligations:
//   - TICKET-TEXT-REVEAL-HELPERS-FORWARD-HEADER-DELETE: migrate all 12 callers
//     to direct module includes, then delete this shim file
//   - TICKET-TEXT-REVEAL-MODULE-UNIT-TESTS: per-module unit tests (cinematic_glow,
//     glyph_layout, text_reveal, typewriter_block) — out-of-scope for Cat-3 minimal-surface
//   - TICKET-TEXT-REVEAL-FONT-FACTORY-PLACEMENT: review whether font_bold /
//     font_regular are better in text_reveal (current home) or in glyph_layout
//     (the shaping module) — deferred to user-spec
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserved across the 4 new modules).

#include "content/common/text/cinematic_glow.hpp"
#include "content/common/text/glyph_layout.hpp"
#include "content/common/text/text_reveal.hpp"
#include "content/common/text/typewriter_block.hpp"
