#include <chronon3d/text/animation/glyph_instance_state.hpp>

#include <chronon3d/text/font_engine.hpp>  // PlacedGlyphRun full type

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// make_initial_glyph_states — initial-state factory
// ═══════════════════════════════════════════════════════════════════════════
//
// Seeds a vector of GlyphInstanceState from a PlacedGlyphRun, one per
// glyph. All animated fields are initialized to identity so each per-frame
// animator mutates from a clean baseline.
//
// Layout positions are copied from the placed run; `glyph_id`, `x`, `y`
// are the only fields read from the source glyphs besides cluster/byte
// info (not used here). All animated offsets, scale, rotation, opacity,
// blur, baseline_shift, fill, stroke, stroke_width start at identity.
// FASE 2a: character_offset is deprecated (always 0) — CharacterOffset
// is now evaluated in the PreShaping phase before HarfBuzz.

std::vector<GlyphInstanceState> make_initial_glyph_states(
    const PlacedGlyphRun& placed
) {
    std::vector<GlyphInstanceState> states;
    reset_glyph_states_in_place(states, placed);
    return states;
}

// ═══════════════════════════════════════════════════════════════════════════
// reset_glyph_states_in_place — hot-path in-place reset
// ═══════════════════════════════════════════════════════════════════════════

void reset_glyph_states_in_place(
    std::vector<GlyphInstanceState>& states,
    const PlacedGlyphRun& placed
) {
    const size_t glyph_count = placed.glyphs.size();

    if (states.size() != glyph_count) {
        states.resize(glyph_count);
    }

    for (size_t i = 0; i < glyph_count; ++i) {
        GlyphInstanceState& gs = states[i];
        gs.glyph_id = placed.glyphs[i].glyph_id;
        gs.layout_position = {placed.glyphs[i].x, placed.glyphs[i].y};
        gs.position = {0.0f, 0.0f, 0.0f};
        gs.scale = {1.0f, 1.0f, 1.0f};
        gs.rotation = {0.0f, 0.0f, 0.0f};
        gs.anchor = {0.0f, 0.0f, 0.0f};
        gs.skew = 0.0f;
        gs.skew_axis = 0.0f;
        gs.opacity = 1.0f;
        gs.blur = 0.0f;
        gs.baseline_shift = 0.0f;
        gs.character_offset = 0;
        gs.fill = {1.0f, 1.0f, 1.0f, 1.0f};
        gs.stroke = {0.0f, 0.0f, 0.0f, 0.0f};
        gs.stroke_width = 0.0f;
    }
}

} // namespace chronon3d
