// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#3 — text_run_shape.hpp
//
// Single-responsibility sub-header for the batched text-run shape slot
// (mutable per-frame glyph animation state + immutable shared layout +
// PR8/9/11 driver payload).
//
// Public surface (verbatim moved from the canonical text_run.hpp):
//   - Forward declarations:
//       class AnimatedTextDocument;
//       class TextLayoutCache;
//   - `struct TextRunShape` with all PR 8/9/B3/11 fields:
//       SharedTextRunLayout           layout;
//       std::vector<GlyphInstanceState> glyphs;
//       TextMaterial                  material;
//       TextPaint                     paint;
//       std::vector<TextShadow>       shadows;
//       std::vector<TextAnimatorSpec>  animators;       // PR 8 driver payload
//       std::shared_ptr<const AnimatedTextDocument> animated_doc;
//       FontEngine*                   engine;          // PR 9 per-frame resample
//       TextLayoutSpec                layout_spec;
//       TextLayoutCache*              cache;           // Fase B3 (was shared_text_layout_cache)
//       SharedTextRunLayout           crossfade_layout;
//       std::vector<GlyphInstanceState> crossfade_glyphs;
//       float                         crossfade_mix;   // PR 11
//
// No new public classes.  No logic changes.  No signature changes.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_layout.hpp>            // SharedTextRunLayout
#include <chronon3d/text/font_engine.hpp>                // FontEngine* (forward-decl works; full def needed for members of FontIdentity/FontSpec? — no, only FontEngine* here)
#include <chronon3d/text/text_animator_property.hpp>     // TextAnimatorSpec, GlyphInstanceState (transitive via animation/)
#include <chronon3d/text/text_material.hpp>              // TextMaterial
#include <chronon3d/scene/model/shape/shape.hpp>           // TextPaint, TextShadow
#include <chronon3d/scene/builders/builder_params.hpp>     // TextLayoutSpec

#include <memory>
#include <vector>

// Forward declarations — sparse pattern (matches pre-M1.5#3 text_run.hpp):
// - std::shared_ptr<const AnimatedTextDocument> only requires the
//   complete type at the implicit ~TextRunShape() instantiation site,
//   which lives in each TU that constructs / destroys a TextRunShape —
//   those TUs typically include the umbrella and transitively reach
//   animated_text_document.hpp.
// - TextLayoutCache* is a raw pointer; full class type only needed by
//   sources that actually call .find()/.store()/.size() on it (whose
//   .cpp includes text_layout_cache.hpp directly).  This keeps the
//   TextRunShape consumer surface lighter (no cache/lru_cache.hpp
//   pulled into every TU that just touches TextRunShape).
namespace chronon3d {
class AnimatedTextDocument;
class TextLayoutCache;
} // namespace chronon3d

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunShape — batched text run with per-glyph animation state
// ═══════════════════════════════════════════════════════════════════════════
//
// Unlike TextShape (which describes a static string with styling), TextRunShape
// combines an immutable shared layout with per-frame animated glyph states.
// This enables After Effects-style text animators (selector + properties)
// to drive glyph-level transforms, opacity, color, blur, etc. without
// creating a separate layer per character.
//
// IMPORTANT: TextRunShape is NOT embedded in the Shape struct (shape.hpp).
// Instead, a dedicated TextRunNode in the render graph will hold it directly.
// This avoids a circular include: fill_style.hpp → shape.hpp → text_run.hpp
// → text_animator_property.hpp → glyph_selector.hpp → animated_value.hpp
// → fill_style.hpp (circular!).

// F0.4 — layout-null invariant:
//   - Default-constructed TextRunShape has layout == nullptr (empty shape).
//   - A shape with non-empty glyphs MUST have a valid (non-null) layout.
//   - Explicit `shape.layout = nullptr` is forbidden on populated shapes.
//   - The renderer and compositor both guard on `layout != nullptr` as a
//     short-circuit for empty runs; this is correct for the default state.
//
// Enforcement: the explicit default constructor documents that null-layout
// is the canonical empty state.  A future private-layout refactor (F4+) will
// add runtime assertion on set_layout().
struct TextRunShape {
    TextRunShape() = default;

    SharedTextRunLayout layout;                  // immutable layout (shared across frames)
    std::vector<GlyphInstanceState> glyphs;       // per-glyph animated state

    TextMaterial material;                         // premium material settings
    TextPaint paint;                              // fill/stroke paint settings
    std::vector<TextShadow> shadows;              // per-layer shadow stack

    // ── Animation driver payload (PR 8) ────────────────────────────────
    // Stored at materialization time so the per-frame driver
    // (update_text_run_shape_per_frame in text_run_driver.hpp) can
    // re-evaluate the AE-style animator stack on each render frame
    // without re-shaping.  Empty vector ⇒ static layout; the renderer
    // treats it as no-op and skips per-frame work.
    //
    // The animator list is owned by the shape so the compositor
    // doesn't have to chase references back into builder state.
    std::vector<TextAnimatorSpec> animators;

    // ── PR 9 — AnimatedTextDocument + helpers for per-frame resample ─
    //
    // When the scene author attached a transition document via
    // `TextRunBuilder::from_animated_document(...)`, the materializer
    // stores both the document (shared_ptr, non-owning extension) and
    // the FontEngine + TextLayoutSpec needed by
    // `apply_active_state_to_text_run_shape` so the per-frame driver
    // can re-sample the doc at every frame and apply transition_text
    // without going back to builder state.  Without these slots the
    // PR 8 driver would only re-evaluate the AE-style animator stack
    // and miss the doc's transition_text changes.
    //
    // `engine` is a raw pointer consistent with the prior
    // PendingTextRun::font_engine convention; the layer is responsible
    // for keeping the engine alive for the shape's lifetime.
    std::shared_ptr<const AnimatedTextDocument> animated_doc{};
    FontEngine* engine{nullptr};
    TextLayoutSpec layout_spec{};

    // Fase B3 — per-shape layout cache (replaces process-wide global).
    // Set by the materializer/engine to the session-owned TextLayoutCache.
    // When nullptr, text re-shaping during animation won't be cached
    // (layouts are built fresh each frame — acceptable for tests).
    TextLayoutCache* cache{nullptr};

    // ── PR 11 — CrossfadeLayouts per-glyph blend state ────────────────
    //
    // Populated by `apply_active_state_to_text_run_shape` ONLY when
    // `AnimatedTextDocument::sample_at` returns transition ==
    // CrossfadeLayouts AND mix is strictly inside (0, 1).  Cleared
    // (reset to nullptr / empty vector) when mix reaches 0 or 1, or
    // when the next keyframe takes ownership (`active` pointer changes
    // to a different keyframe).  Empty slots ⇒ no crossfade work; the
    // per-frame driver and the compositor both short-circuit on that.
    //
    // `crossfade_layout` holds the SharedTextRunLayout for the OUTGOING
    // document so the executor can read glyph positions without paying
    // a shaping cost.  It is built lazily on first frame inside the
    // gap via `build_text_run` (with the shared TextLayoutCache); the
    // cache key naturally aligns with what `prewarm_text_run_layout_for_frame`
    // populates ahead of time.
    //
    // `crossfade_glyphs` mirrors `glyphs`: a per-glyph animated state
    // vector of length `crossfade_layout->placed.glyphs.size()`, seeded
    // from `make_initial_glyph_states` and re-evaluated each frame via
    // `evaluate_animator_stack_into` so the AE-style animator stack
    // applies to BOTH sides during the gap.
    //
    // `crossfade_mix` mirrors `state.mix`: 0 = fully active side, 1 =
    // fully next side.  The compositor folds this into per-glyph
    // opacity: outgoing side fades by (1 - mix), incoming side by mix.
    // Stored so the compositor (and the hash fold) can read the value
    // without re-sampling the animated_doc — it stays stable across
    // cache-key lookups and the per-frame driver.
    //
    // Lifecycle contract — apply + driver:
    //   1. apply_active_state_to_text_run_shape (PR 11) populates
    //      crossfade_layout + crossfade_glyphs on entering the gap,
    //      updates crossfade_mix on every frame INSIDE the gap, and
    //      clears all three slots on transitioning OUT of the gap.
    //   2. update_text_run_shape_per_frame runs the animator stack
    //      against crossfade_glyphs on every frame inside the gap
    //      so per-glyph animators (transform / opacity / blur) apply
    //      to BOTH sides symmetrically.
    SharedTextRunLayout crossfade_layout{};                       // nullptr when no crossfade
    std::vector<GlyphInstanceState> crossfade_glyphs{};          // empty when no crossfade
    float crossfade_mix{0.0f};                                   // current blend factor
};

} // namespace chronon3d
