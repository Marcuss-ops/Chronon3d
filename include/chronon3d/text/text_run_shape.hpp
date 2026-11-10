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
//       SharedTextRunLayout           dissolve_layout;
//       std::vector<GlyphInstanceState> dissolve_glyphs;
//       float                         dissolve_mix;   // PR 11
//
// No new public classes.  No logic changes.  No signature changes.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_layout.hpp>            // SharedTextRunLayout
#include <chronon3d/text/font_engine.hpp>                // FontEngine* (forward-decl works; full def needed for members of FontIdentity/FontSpec? — no, only FontEngine* here)
#include <chronon3d/text/text_animator_property.hpp>     // TextAnimatorSpec, GlyphInstanceState (transitive via animation/)
#include <chronon3d/text/text_material.hpp>              // TextMaterial
#include <chronon3d/scene/model/shape/shape.hpp>           // TextPaint, TextShadow
#include <chronon3d/text/text_layout_spec.hpp>     // TextLayoutSpec
#include <chronon3d/text/text_placement.hpp>               // TextPlacementKind

#include <algorithm>
#include <cmath>
#include <cstddef>
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
struct PreShapingUnitMap;
} // namespace chronon3d

namespace chronon3d {

// ── P1-10 — text insertion caret ────────────────────────────────────────
// A caret is not a free-standing shape: it is resolved from the same placed
// glyph run as the text, so it follows alignment, wrapping, tracking and
// per-glyph animation.  The renderer draws it as a text-space quad; the GPU
// text lane rejects it until the native pass can reproduce that contract.
struct TextCaretSpec {
    bool enabled{false};
    std::size_t byte_index{0};
    f32 width{2.0f};
    f32 height{0.0f};            // 0 = font ascent + descent
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 opacity{1.0f};
    i32 blink_period_frames{0};  // 0 = steady caret
    f32 blink_duty{0.5f};
    f64 frame{0.0};              // sampled frame, used only for blinking
};

struct TextCaretRect {
    bool visible{false};
    f32 x{0.0f};
    f32 y{0.0f};
    f32 width{0.0f};
    f32 height{0.0f};
};

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
    TextPlacementKind placement_kind{TextPlacementKind::Absolute};

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
    // Identity of the document (text + spans + paint ranges) currently
    // compiled into `layout` / `dissolve_layout`.  The driver compares these
    // to the sampled keyframe document so a style-only change still rebuilds.
    u64 animated_document_hash{0};
    // False means `layout` was compiled without spans/paint ranges, so a
    // target hash of 0 on a fresh shape is still a valid cache hit for an
    // unstyled Hold frame.  True forces the hash comparison below.
    bool animated_document_style{false};
    u64 dissolve_document_hash{0};
    TextCaretSpec caret{};

    // Stable authoring source for animated PreShaping properties. The layout
    // source is transformed, so it cannot be used as the next frame's input.
    std::string pre_shaping_source{};
    bool has_pre_shaping_source{false};

    // ADR-037 — cacheable canonical source map used to resolve selectors for
    // selector-scoped Character Offset / Character Value.  Keyed by the
    // authored source plus the layout identity so a font or layout change
    // cannot reuse stale units.  Null means no selector-scoped PreShaping
    // animator is active (the run-level path applies).
    std::shared_ptr<const PreShapingUnitMap> pre_shaping_unit_map{};
    std::string pre_shaping_unit_map_source{};
    std::string pre_shaping_unit_map_key{};

    // P2-05 — authored spacing baseline for PreLayout animators.  The driver
    // evaluates reflow_word_spacing/reflow_line_spacing on top of these values;
    // keeping them separate prevents repeated frames from accumulating the
    // animated amount into the authored paragraph style.
    f32 reflow_word_spacing_base{0.0f};
    f32 reflow_line_spacing_base{0.0f};

    // Fase B3 — per-shape layout cache (replaces process-wide global).
    // Set by the materializer/engine to the session-owned TextLayoutCache.
    // When nullptr, text re-shaping during animation won't be cached
    // (layouts are built fresh each frame — acceptable for tests).
    TextLayoutCache* cache{nullptr};

    // ── PR 11 — DissolveLayouts per-glyph blend state ────────────────
    //
    // Populated by `apply_active_state_to_text_run_shape` ONLY when
    // `AnimatedTextDocument::sample_at` returns transition ==
    // DissolveLayouts AND mix is strictly inside (0, 1).  Cleared
    // (reset to nullptr / empty vector) when mix reaches 0 or 1, or
    // when the next keyframe takes ownership (`active` pointer changes
    // to a different keyframe).  Empty slots ⇒ no crossfade work; the
    // per-frame driver and the compositor both short-circuit on that.
    //
    // `dissolve_layout` holds the SharedTextRunLayout for the OUTGOING
    // document so the executor can read glyph positions without paying
    // a shaping cost.  It is built lazily on first frame inside the
    // gap via `build_text_run` (with the shared TextLayoutCache); the
    // cache key naturally aligns with what `prewarm_text_run_layout_for_frame`
    // populates ahead of time.
    //
    // `dissolve_glyphs` mirrors `glyphs`: a per-glyph animated state
    // vector of length `dissolve_layout->placed.glyphs.size()`, seeded
    // from `make_initial_glyph_states` and re-evaluated each frame via
    // `evaluate_animator_stack_into` so the AE-style animator stack
    // applies to BOTH sides during the gap.
    //
    // `dissolve_mix` mirrors `state.mix`: 0 = fully outgoing side, 1 =
    // fully incoming side.  The compositor folds this into per-glyph
    // opacity: outgoing side fades by (1 - mix), incoming side by mix.
    // Stored so the compositor (and the hash fold) can read the value
    // without re-sampling the animated_doc — it stays stable across
    // cache-key lookups and the per-frame driver.
    //
    // Lifecycle contract — apply + driver:
    //   1. apply_active_state_to_text_run_shape (PR 11) populates
    //      dissolve_layout + dissolve_glyphs on entering the gap,
    //      updates dissolve_mix on every frame INSIDE the gap, and
    //      clears all three slots on transitioning OUT of the gap.
    //   2. update_text_run_shape_per_frame runs the animator stack
    //      against dissolve_glyphs on every frame inside the gap
    //      so per-glyph animators (transform / opacity / blur) apply
    //      to BOTH sides symmetrically.
    SharedTextRunLayout dissolve_layout{};                       // nullptr when no crossfade
    std::vector<GlyphInstanceState> dissolve_glyphs{};          // empty when no crossfade
    float dissolve_mix{0.0f};                                   // current blend factor
};

namespace caret_detail {

[[nodiscard]] inline bool is_utf8_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

/// Snap an authored byte insertion point to a UTF-8 code-point boundary.  A
/// caret inside a multibyte code point has no valid insertion position, so it
/// moves back to the leading byte instead of splitting the code point.
[[nodiscard]] inline std::size_t normalize_utf8_caret_index(
    std::size_t index, const std::string& source) noexcept {
    if (source.empty()) return index;
    if (index > source.size()) index = source.size();
    while (index > 0 && index < source.size() && is_utf8_continuation(source[index])) {
        --index;
    }
    return index;
}

/// Direction of the resolver run that shaped this glyph.  It is carried by
/// ShapedFontSpan after shaping; visual glyph order must never decide it.
[[nodiscard]] inline TextDirection span_direction_for_glyph(
    const TextRunLayout& layout, std::size_t glyph_index) noexcept {
    for (const auto& span : layout.font_spans) {
        if (glyph_index >= span.glyph_begin && glyph_index < span.glyph_end)
            return span.direction;
    }
    return layout.direction == TextDirection::RTL ? TextDirection::RTL
                                                  : TextDirection::LTR;
}

} // namespace caret_detail

/// Resolve the authored insertion point against the placed glyph run.  The
/// caret sits on the logical leading edge of the cluster containing byte_index
/// (left for LTR, right for RTL); an index at the run end sits on the final
/// logical edge.  Interior positions of a ligature or combining-mark cluster
/// snap to an edge instead of splitting it.  Coordinates are run-local and use
/// the same origin as PlacedGlyph::x/y.
[[nodiscard]] inline TextCaretRect resolve_text_caret_rect(const TextRunShape& shape) {
    TextCaretRect rect;
    if (!shape.caret.enabled || !shape.layout || shape.layout->placed.glyphs.empty())
        return rect;
    const auto& placed = shape.layout->placed;
    const auto& glyphs = placed.glyphs;
    const std::size_t index = caret_detail::normalize_utf8_caret_index(
        shape.caret.byte_index, shape.layout->source_text);

    const PlacedGlyph* anchor = &glyphs.back();
    f32 x = anchor->x;
    bool resolved = false;

    if (!placed.clusters.empty()) {
        const PlacedGlyphRun::Cluster* cluster = nullptr;
        for (const auto& candidate : placed.clusters) {
            if (index >= candidate.byte_offset &&
                index < candidate.byte_offset + candidate.byte_len) {
                cluster = &candidate;
                break;
            }
        }
        // A boundary exactly at a cluster end belongs to the next logical
        // cluster; past the run it is the final boundary of the last cluster.
        if (cluster == nullptr) {
            for (const auto& candidate : placed.clusters) {
                if (index < candidate.byte_offset) {
                    cluster = &candidate;
                    break;
                }
            }
        }
        if (cluster == nullptr) cluster = &placed.clusters.back();

        const std::size_t begin = std::min(cluster->start_glyph, glyphs.size());
        const std::size_t end = std::min(cluster->end_glyph, glyphs.size());
        if (begin < end) {
            f32 left = glyphs[begin].x;
            f32 right = glyphs[begin].x + glyphs[begin].advance_x;
            for (std::size_t g = begin + 1; g < end; ++g) {
                left = std::min(left, glyphs[g].x);
                right = std::max(right, glyphs[g].x + glyphs[g].advance_x);
            }
            anchor = &glyphs[begin];
            const bool rtl = caret_detail::span_direction_for_glyph(
                *shape.layout, begin) == TextDirection::RTL;
            const bool at_logical_start = index <= cluster->byte_offset;
            x = at_logical_start ? (rtl ? right : left) : (rtl ? left : right);
            resolved = true;
        }
    }

    if (!resolved) {
        // Legacy layouts without a cluster map: keep the glyph scan, but the
        // run direction still selects the edge.
        std::size_t anchor_index = glyphs.size() - 1;
        for (std::size_t g = 0; g < glyphs.size(); ++g) {
            if (index >= glyphs[g].byte_offset &&
                index <= glyphs[g].byte_offset + glyphs[g].byte_len) {
                anchor_index = g;
                break;
            }
            if (index > glyphs[g].byte_offset) anchor_index = g;
        }
        anchor = &glyphs[anchor_index];
        const bool rtl = caret_detail::span_direction_for_glyph(
            *shape.layout, anchor_index) == TextDirection::RTL;
        const bool at_logical_start = index <= anchor->byte_offset;
        f32 left = anchor->x;
        f32 right = anchor->x + anchor->advance_x;
        if (!rtl && index > anchor->byte_offset && anchor->byte_len > 0) {
            const f32 fraction = std::min(
                1.0f, static_cast<f32>(index - anchor->byte_offset) /
                          static_cast<f32>(anchor->byte_len));
            left += anchor->advance_x * fraction;
            right = left;
        }
        x = at_logical_start ? (rtl ? right : left) : (rtl ? left : right);
    }

    f32 height = shape.caret.height > 0.0f
        ? shape.caret.height : placed.ascent + placed.descent;
    if (!(height > 0.0f)) height = shape.layout->font_size;

    bool visible = true;
    if (shape.caret.blink_period_frames > 0) {
        const double period = static_cast<double>(shape.caret.blink_period_frames);
        const double phase = std::fmod(std::max(0.0, shape.caret.frame), period);
        visible = phase < period * std::clamp(shape.caret.blink_duty, 0.0f, 1.0f);
    }
    rect.visible = visible && shape.caret.opacity > 0.0f;
    rect.x = x;
    rect.y = anchor->y - placed.ascent;
    rect.width = std::max(0.0f, shape.caret.width);
    rect.height = height;
    return rect;
}

/// Per-frame text state kept separate from the immutable shape payload.
/// Layouts are shared and glyph state is reused by the executor scratch; the
/// static material/animation/document data remains owned by TextRunShape.
/// This is the data-plane representation used by render_text_run_item().
struct TextRunFrameState {
    // Reusable adapter for legacy renderer interfaces. The adapter owns the
    // mutable glyph vectors for this worker; it is never copied from
    // TextRunShape wholesale.
    TextRunShape render_shape{};
    const TextRunShape* source_identity{nullptr};
};

} // namespace chronon3d
