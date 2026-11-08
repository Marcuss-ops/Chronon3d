#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>

#include <vector>

namespace chronon3d {

// Forward declaration — full type lives in chronon3d/text/font_engine.hpp.
// This header uses PlacedGlyphRun only by reference, so a forward
// declaration keeps glyph_instance_state.hpp lightweight (font_engine.hpp
// transitively pulls HarfBuzz + FreeType + script bindings).
struct PlacedGlyphRun;

// ═══════════════════════════════════════════════════════════════════════════
// GlyphInstanceState — resolved per-glyph state for one frame
// ═══════════════════════════════════════════════════════════════════════════
//
// This is the output of evaluating all animators for a single glyph.
// The renderer uses this to position and style each glyph in a TextRun.
// One instance per glyph in the active TextRun; mutated in place by
// `evaluate_animator` per frame.
//
// `layout_position` is the immutable base position from the shaping pass
// (preserved across frames); animated `position`, `scale`, `rotation`,
// `opacity`, etc. are what each animator mutates per frame.

// ── Per-glyph paint extensions (P1-06 / P1-07) ───────────────────────────
// These are intentionally stored on GlyphInstanceState rather than on
// TextRunShape: an animator may select any subset of glyphs, so a run-level
// shadow/glow/gradient cannot express the authored result.  The software
// raster consumes them per glyph; the native GPU text lane fails closed
// until it can reproduce the same pixel contract.
struct GlyphShadowState {
    bool enabled{false};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    Vec2 offset{0.0f, 0.0f};       // pixels, glyph-local
    f32 blur{0.0f};                // Gaussian radius in pixels
    f32 opacity{1.0f};
};

struct GlyphGlowState {
    bool enabled{false};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 radius{0.0f};              // Gaussian radius in pixels
    f32 intensity{1.0f};
};

struct GlyphGradientState {
    bool enabled{false};
    Color start{1.0f, 1.0f, 1.0f, 1.0f};
    Color end{0.0f, 0.0f, 0.0f, 1.0f};
    f32 angle_degrees{0.0f};       // 0 = left→right, 90 = top→bottom
};

struct GlyphInstanceState {
    u32 glyph_id{0};                  // font-specific glyph index

    Vec2 layout_position{0.0f, 0.0f}; // base position from text layout
    Vec3 position{0.0f, 0.0f, 0.0f};  // animated position offset
    Vec3 scale{1.0f, 1.0f, 1.0f};     // animated scale
    Vec3 rotation{0.0f, 0.0f, 0.0f};  // animated rotation (degrees, XYZ)
    Vec3 anchor{0.0f, 0.0f, 0.0f};    // animated anchor point

    f32 skew{0.0f};                    // skew angle
    f32 skew_axis{0.0f};              // skew axis angle
    f32 opacity{1.0f};                // final opacity
    f32 blur{0.0f};                   // blur radius
    f32 baseline_shift{0.0f};         // vertical shift from baseline
    // FASE 2a: character_offset is always 0.  CharacterOffsetProperty
    // is now evaluated in the PreShaping phase (transforms source text
    // before HarfBuzz).  Kept for ABI compat; remove in a follow-up.
    i32 character_offset{0};          // [deprecated] always 0

    Color fill{1.0f, 1.0f, 1.0f, 1.0f};     // final fill color
    Color background{0.0f, 0.0f, 0.0f, 0.0f}; // background rect color (alpha=0 → disabled)
    f32 background_padding_em{0.0f};
    f32 background_radius_em{0.0f};
    Color stroke{0.0f, 0.0f, 0.0f, 0.0f};     // stroke color (alpha=0 → disabled)
    f32 stroke_width{0.0f};                 // stroke width

    GlyphShadowState shadow{};
    GlyphGlowState glow{};
    GlyphGradientState fill_gradient{};
};

// ═══════════════════════════════════════════════════════════════════════════
// Initial glyph state factory
// ═══════════════════════════════════════════════════════════════════════════

/// Build initial glyph states from a PlacedGlyphRun.
/// Each glyph gets its layout_position from the placed run; all animated
/// fields are seeded to identity (zero position, neutral scale, full
/// opacity, identity fill).
///
/// Implementation lives in `src/text/animation/glyph_instance_state.cpp`.
[[nodiscard]] std::vector<GlyphInstanceState> make_initial_glyph_states(
    const PlacedGlyphRun& placed
);

/// Reset `states` to initial values for all glyphs in `placed`.
///
/// If `states.size() != placed.glyphs.size()`, resizes the vector first.
/// Otherwise each element is reset in place — zero allocations when the
/// glyph count is stable across frames.  This is the hot-path alternative
/// to `make_initial_glyph_states` for the per-frame animator driver.
///
/// Implementation lives in `src/text/animation/glyph_instance_state.cpp`.
void reset_glyph_states_in_place(
    std::vector<GlyphInstanceState>& states,
    const PlacedGlyphRun& placed
);

} // namespace chronon3d
