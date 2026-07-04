// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#3 — text_run_layout.hpp
//
// Single-responsibility sub-header for the immutable Layout data slot.
// Lives next to text_layout_identity.hpp, text_run_shape.hpp,
// text_layout_cache.hpp, text_run_hash.hpp under include/chronon3d/text/.
//
// Public surface (verbatim moved from the canonical text_run.hpp):
//   - `using Bcp47LanguageTag = std::string;`     (TICKET-103a)
//   - `using TextShapingFeatures = std::string;`  (TICKET-103a)
//   - `struct ShapedFontSpan`                     (per-glyph font identity blocks)
//   - `struct TextRunLayout`                      (immutable layout data, with hash slots)
//   - `using SharedTextRunLayout = std::shared_ptr<const TextRunLayout>;`
//   - Forward-declaration comments for the planned (DEFERRED post-baseline)
//     `CompiledTextRun` and `PerFrameBlurTiers` types — these are
//     documentation-only and do not commit a header surface today
//     (per feature freeze).
//
// No new public classes.  No logic changes.  No signature changes.  The
// umbrella `text_run.hpp` re-includes this header so every existing
// `#include <chronon3d/text/text_run.hpp>` keeps the same visibility.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/font_engine.hpp>      // FontSpec, FontIdentity, PlacedGlyphRun, TextWrap, TextDirection
#include <chronon3d/text/glyph_selector.hpp>   // TextUnitMap (PlacedGlyphRun => TextUnitMap via glyph_selector)
#include <chronon3d/core/types/types.hpp>       // Vec2, f32, u64, u32

#include <cstddef>                              // std::uint32_t transitively for ShapedFontSpan fields
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-103a — freeze-minimum cat-3 type aliases used by TextLayoutRequest,
// TextRunLayout, and TextLayoutCacheKey.  Same bytewise layout as
// std::string / TextDirection — no new classes introduced, just explicit
// semantic names.  Promoted from src/text/aliases.hpp's
// Bcp47LanguageTag alias (which stays internal-and-unused) so the
// TICKET-103a cat-3 refactor can extend the existing TextLayoutRequest
// POD struct with named fields without expanding the public type surface.
// ═══════════════════════════════════════════════════════════════════════════

/// RFC 5646 BCP-47 language tag (e.g. "en", "en-US", "ar", "zh-Hant-HK").
/// Passes through to HarfBuzz hb_language_from_string() unchanged.
/// Same bytewise layout as std::string.
using Bcp47LanguageTag = std::string;

/// OpenType / HarfBuzz shaping features string (e.g. "kern=1,liga=1,dlig=0").
/// Empty default = "use the font's natural feature set".  Same bytewise
/// layout as std::string.  Held as a single string for the freeze-minimum
/// scope; the more elaborate per-feature toggle struct (i32 tag + u8
/// value + u8 start/end + u16 reserved fields, à la hb_feature_t) is
/// TICKET-093 (cat-violator post-baseline-verde).
using TextShapingFeatures = std::string;

// ═══════════════════════════════════════════════════════════════════════════
// TextRunLayout — immutable layout data for a shaped text run
// ═══════════════════════════════════════════════════════════════════════════
//
// Produced once per unique (text, font, size, tracking, wrap) combination.
// Shared across frames via a std::shared_ptr, so multiple TextRunShape
// instances (or multiple frames of the same animated text) reuse the same
// layout without re-shaping.
//
// Mutability: once created, the layout is immutable. Only the per-frame
// animated glyph states change (stored separately in GlyphInstanceState).

// ═══════════════════════════════════════════════════════════════════════════
// ShapedFontSpan — per-glyph font identity blocks (Phase 1.4)
// ═══════════════════════════════════════════════════════════════════════════
//
// A contiguous run of glyphs in `TextRunLayout::placed.glyphs` that share
// the SAME font identity.  Used by the renderer to switch BLFont at span
// boundaries WITHOUT re-allocating a fresh TextRunLayout for each font.
//
// Closure contract for verdict Issue #3 (Fase 1.4 additive):
//   compile_text_layout() populates `TextRunLayout::font_spans` so that
//   `glyphs[span.glyph_begin .. span.glyph_end)` are guaranteed to have
//   been shaped with `span.font` (FontIdentity).  The renderer iterates
//   the spans, resolves a BLFont per UNIQUE `span.font` (typically 1 —
//   single-font paragraphs — sometimes 2+ — bidirectional Latin/Hebrew
//   runs, or multi-font paragraphs mixing static bases), and switches
//   BLFont at span boundaries.  The `font_size` dimension is NOT in
//   FontIdentity (it's a layout concern — see FontIdentity header doc)
//   so size variation does NOT split a single span.
//
// Empty paragraphs and unresolved paragraphs (`placed.glyphs.empty()`)
// have an empty `font_spans` vector — the renderer's loop over
// `tier_glyphs` short-circuits to nothing in that case.
struct ShapedFontSpan {
    FontIdentity   font;        // identity of the font for glyphs [glyph_begin, glyph_end)
    std::uint32_t  glyph_begin{0};  // inclusive index into TextRunLayout::placed.glyphs
    std::uint32_t  glyph_end{0};    // EXCLUSIVE index
};

struct TextRunLayout {
    std::string source_text;                     // original UTF-8 source text
    FontSpec font;                               // primary font (drives cache key; may NOT cover all glyphs — see font_spans)
    f32 font_size{72.0f};                        // requested font size in pixels

    PlacedGlyphRun placed;                       // shaped + positioned glyphs
    TextUnitMap units;                           // glyph → grapheme/word/line maps

    Vec2 bounds{0.0f, 0.0f};                     // total bounding box (width, height)
    f32 line_height{0.0f};                       // font line height in pixels

    /// Per-glyph-blocks telling the renderer which font identity each
    /// contiguous glyph range uses.  For a single-font paragraph this
    /// is exactly ONE span [0, placed.glyphs.size()).  For a multi-font
    /// paragraph or a bidi paragraph that resolved into multiple
    /// ResolvedTextRun entries of the same font, this has one span per
    /// run (in glyph-index order).  EMPTY when the paragraph has zero
    /// glyphs.  Renderer iterates this to pre-resolve BLFont instances
    /// and switch at boundaries — see text_run_processor.cpp.
    std::vector<ShapedFontSpan> font_spans;

    /// Shaping parameters used (for cache key hashing / diagnostics)
    f32 tracking{0.0f};                          // per-cluster tracking in pixels
    TextWrap wrap{TextWrap::None};               // wrapping mode
    TextDirection direction{TextDirection::Auto};
    Bcp47LanguageTag language;                  // BCP-47 language tag (TICKET-103a: alias of std::string)
    TextShapingFeatures features;               // OT shaping features (TICKET-103a: new field)

    /// Compute a hash of the layout content (text + font + shaping + wrapping).
    /// Stable across different materials/strokes on the same text.
    [[nodiscard]] u64 layout_hash() const;

    /// Compute a hash of only the shaping parameters (font, size, wrap, direction).
    /// Excludes the source text — useful for cache partitioning.
    [[nodiscard]] u64 shaping_hash() const;
};

/// Immutable, shared ownership of a TextRunLayout.
/// Multiple frames/components can hold the same pointer without copying.
using SharedTextRunLayout = std::shared_ptr<const TextRunLayout>;

// ═══════════════════════════════════════════════════════════════════════════
// Fase C1 — CompiledTextRun (planned, deferred to post-feature-freeze)
// ═══════════════════════════════════════════════════════════════════════════
//
// The render hot path (draw_text_run) currently recomputes several
// lookup structures on every frame that depend ONLY on the immutable
// TextRunLayout.  These should be pre-compiled once and cached:
//
// Planned CompiledTextRun structure:
//
//   struct CompiledTextRun {
//       SharedTextRunLayout    layout;          // immutable shared layout
//       std::vector<ResolvedFontSpan> spans;    // pre-resolved font handles
//       std::vector<std::size_t> glyph_to_span; // per-glyph → span index
//   };
//
//   struct ResolvedFontSpan {
//       FontFaceHandle handle;   // pre-resolved via TextRenderResources
//       // NOTE: BLFont is a software-backend dependency. If CompiledTextRun
//       // moves into include/chronon3d/, use a type-erased handle or
//       // opaque pointer to avoid coupling text_core to Blend2D.
//       void*          blfont_opaque;  // type-erased BLFont handle
//       std::uint32_t  glyph_begin, glyph_end;  // same as ShapedFontSpan
//   };
//
//   // Per-frame blur tier classification.  blur values come from
//   // animated GlyphInstanceState, so tiers are computed O(G) per frame.
//   // This struct stores the result — it's not pre-compiled.
//   struct PerFrameBlurTiers {
//       std::array<std::vector<u32>, 5> tiers;
//   };
//
// Migration plan (post-feature-freeze):
//   1. CompiledTextRun stored alongside TextRunShape (or inside it)
//   2. compile(TextRunLayout, TextRenderResources&) → CompiledTextRun
//   3. draw_text_run receives const CompiledTextRun& instead of
//      resolving font spans inline
//   4. ResolvedFontSpan::handle stays valid for the engine lifetime
//      (TextRenderResources owns the underlying FontFaceHandle pool)
//
// Blocked by: new types in public headers → violates feature freeze.
// ═══════════════════════════════════════════════════════════════════════════

} // namespace chronon3d
