#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_resolver.hpp — TextDocument resolution pipeline
//
// PR 6 of the text pipeline.  Converts a TextDocument into a resolved tree
// of text runs, each with a specific font (after fallback), text direction
// (after bidi segmentation), and text content (scoped to paragraph/span/bidi
// boundaries).
//
// Pipeline:
//   TextDocument
//       ↓  split_paragraphs()          ← already in text_document.cpp
//       ↓  resolve_fallback_fonts()     ← font substitution
//       ↓  resolve_text_run_tree()      ← spans → bidi → resolved runs
//       ↓  resolve_and_shape()          ← HarfBuzz shaping → PlacedGlyphRun
//       ↓  concatenate + ParagraphComposer → TextRunLayout (text_run_builder)
//
// Usage:
//   FontEngine engine;
//   TextDocument doc = ...;
//   doc.split_paragraphs();
//   auto tree = resolve_text_run_tree(doc, engine);
//
//   for (const auto& par : tree.paragraphs) {
//       for (const auto& run : par.runs) {
//           // shape run.text with run.font, run.direction
//       }
//   }
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace chronon3d {

// ── ResolvedTextRun — a single directional run with a resolved font ────

/// Represents one contiguous span of text that shares a font face,
/// text direction, and styling.  This is the atomic unit fed to
/// HarfBuzz for shaping.
struct ResolvedTextRun {
    /// The UTF-8 text for this run (a substring of the original document).
    std::string text;

    /// The font to shape this run with.  Already resolved through the
    /// fallback chain — guaranteed to be a loadable font (or empty if
    /// no fallback exists, which the caller should treat as invisible).
    FontSpec font;

    /// LTR or RTL (never Auto).  Determined by the bidi segmenter or
    /// the paragraph's TextDirection override.
    TextDirection direction{TextDirection::LTR};

    /// Byte offset of this run's text in the original TextDocument::utf8.
    size_t byte_offset{0};

    /// Byte length of this run's text (same as text.size()).
    size_t byte_len{0};

    /// The paragraph style governing this run's layout.
    ParagraphStyle paragraph_style{};
};

// ── ResolvedParagraph — one paragraph with its resolved runs ───────────

/// Holds a paragraph's text runs and its paragraph-level layout style.
struct ResolvedParagraph {
    /// Runs in logical (storage) order, NOT visual order.
    /// For RTL runs, the shaper will reverse glyph placement.
    std::vector<ResolvedTextRun> runs;

    /// The paragraph style for this paragraph (from ParagraphRange).
    ParagraphStyle style{};
};

// ── ResolvedTextTree — the fully resolved document tree ────────────────

/// The output of resolve_text_run_tree().  Contains one ResolvedParagraph
/// per paragraph in the TextDocument, each subdivided into ResolvedTextRun
/// entries by bidi direction and font changes.
struct ResolvedTextTree {
    std::vector<ResolvedParagraph> paragraphs;

    /// Convenience: total number of runs across all paragraphs.
    [[nodiscard]] size_t total_runs() const {
        size_t n = 0;
        for (const auto& p : paragraphs) {
            n += p.runs.size();
        }
        return n;
    }
};

// ── ShapedParagraph — one paragraph with HarfBuzz-shaped runs ─────────

/// Holds the PlacedGlyphRun outputs after shaping all ResolvedTextRuns
/// of a paragraph.  Ready for concatenation + composition.
struct ShapedParagraph {
    /// One PlacedGlyphRun per ResolvedTextRun in the original paragraph.
    /// Order matches ResolvedParagraph::runs.
    std::vector<PlacedGlyphRun> shaped_runs;

    /// The paragraph style from the source paragraph.
    ParagraphStyle style{};
};

// ── ShapedTextTree — resolved + shaped document tree ──────────────────

/// The output of resolve_and_shape().  Contains one ShapedParagraph per
/// paragraph, each with HarfBuzz-shaped PlacedGlyphRun entries.
struct ShapedTextTree {
    std::vector<ShapedParagraph> paragraphs;

    [[nodiscard]] size_t total_runs() const {
        size_t n = 0;
        for (const auto& p : paragraphs) {
            n += p.shaped_runs.size();
        }
        return n;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// resolve_fallback_fonts
// ═══════════════════════════════════════════════════════════════════════════

/// Resolve a FontSpec through the fallback chain using a FontEngine.
///
/// If `primary` can be loaded by the engine, it is returned unchanged.
/// Otherwise, the function attempts a sequence of fallbacks:
///   1. The primary font (unchanged)
///   2. A font with the same font_family but a generic path (empty path,
///      hoping the engine has a family→path mapping)
///   3. A platform-generic serif fallback
///   4. A platform-generic sans-serif fallback
///   5. The primary font returned as-is (caller should check can_load())
///
/// @param primary  The FontSpec to resolve.
/// @param engine   FontEngine used to check font availability.
/// @return A FontSpec that is more likely to be loadable.
[[nodiscard]] FontSpec resolve_fallback_fonts(
    FontSpec primary,
    FontEngine& engine
);

// ═══════════════════════════════════════════════════════════════════════════
// resolve_text_run_tree
// ═══════════════════════════════════════════════════════════════════════════

/// Resolve a TextDocument into a tree of paragraphs and directional runs.
///
/// The caller MUST call doc.split_paragraphs() before passing the document
/// to this function.  (split_paragraphs() is non-const, so the resolver
/// cannot auto-split on a const reference.)
///
/// Steps:
///   1. Validate paragraphs are pre-populated.
///   2. For each paragraph:
///      a. Collect TextStyleSpan overrides for the paragraph's byte range.
///      b. Split into font-homogeneous sub-ranges.
///      c. Run bidi segmentation (segment_bidi_runs) on each sub-range.
///      d. Resolve font fallback for each bidi run.
///      e. Emit ResolvedTextRun entries.
///
/// @param doc    The TextDocument to resolve.  Must be validated.
/// @param engine FontEngine for font fallback resolution.
/// @return A ResolvedTextTree with one ResolvedParagraph per document
///         paragraph, each containing resolved runs.
[[nodiscard]] ResolvedTextTree resolve_text_run_tree(
    const TextDocument& doc,
    FontEngine& engine
);

// ═══════════════════════════════════════════════════════════════════════════
// shape_resolved_run
// ═══════════════════════════════════════════════════════════════════════════

/// Shape a single ResolvedTextRun through HarfBuzz and resolve glyph
/// placements with tracking.
///
/// @param run      The resolved text run (font + direction + text).
/// @param engine   FontEngine for HarfBuzz shaping.
/// @param tracking Per-cluster tracking spacing in pixels.
/// @return A PlacedGlyphRun with positioned glyphs and cluster info.
///         Returns an empty run (no glyphs) if the font fails to load.
[[nodiscard]] PlacedGlyphRun shape_resolved_run(
    const ResolvedTextRun& run,
    FontEngine& engine,
    float tracking = 0.0f
);

// ═══════════════════════════════════════════════════════════════════════════
// resolve_and_shape
// ═══════════════════════════════════════════════════════════════════════════

/// Resolve a TextDocument AND shape every run through HarfBuzz.
/// Convenience wrapper that calls resolve_text_run_tree() then
/// shape_resolved_run() on every ResolvedTextRun.
///
/// @param doc      The TextDocument (paragraphs must be pre-split).
/// @param engine   FontEngine for fallback + shaping.
/// @param tracking Per-cluster tracking in pixels.
/// @return A ShapedTextTree with one ShapedParagraph per paragraph,
///         each containing PlacedGlyphRun entries ready for the compositor.
[[nodiscard]] ShapedTextTree resolve_and_shape(
    const TextDocument& doc,
    FontEngine& engine,
    float tracking = 0.0f
);

// ═══════════════════════════════════════════════════════════════════════════
// paragraph_is_multi_font — single canonical multi-font detector (CR#5)
// ═══════════════════════════════════════════════════════════════════════════
//
// Hoisted from `compile_text_layout` (Fase 1.5) into the resolver header
// where the font hierarchy is canonical.  Both the per-paragraph call site
// in `compile_text_layout` AND the per-paragraph wrapper pre-check in
// `build_text_run` use this single helper, so a future distinguishing
// field (font_stretch, font_variation, language) added to FontSpec is
// locally extendable: bump the std::tie column list below AND
// `FontSpec::operator==` in font_engine.hpp.  No edit needed in
// text_run_builder.cpp / scene / builders.
///
/// A paragraph is multi-font iff it carries MORE THAN ONE run AND any
/// pair of runs differs in the FontSpec fields listed in std::tie.  Note
/// that `font_size` is EXPLICITLY NOT in the comparison: font_size is a
/// layout concern (superscript, drop-cap scaling, mid-paragraph emphasis
/// gradients) and varying font_size on a SINGLE FontSpec must NOT trip
/// multi-font detection.  Likewise bidi-segmented multi-run paragraphs
/// that share a font (e.g. LTR + RTL of the same family) intentionally
/// return false so existing mixed-direction paragraphs continue to
/// compile normally.
///
/// noexcept because FontSpec fields are all POD-like (string + int +
/// string) with no throwing operations.
[[nodiscard]] inline bool paragraph_is_multi_font(
    const ResolvedParagraph& para
) noexcept {
    // ── Single-run + empty paragraphs are NEVER multi-font ─────────
    // Multi-font is defined as "the paragraph has been resolved into
    // > 1 run AND those runs disagree on font identity".  The trivial
    // cases (0 or 1 runs) cannot satisfy the second conjunct.
    if (para.runs.size() < 2) return false;

    // ── Compare against the first run as canonical ─────────────────
    // (O(N) pairwise check, equivalent to `adjacent_find`-with-unequal
    // under transitive equality of FontSpec across the same chain).
    const FontSpec& first = para.runs.front().font;
    for (std::size_t i = 1; i < para.runs.size(); ++i) {
        const FontSpec& f = para.runs[i].font;

        // ── FUTURE-PROOF FontSpec equality via std::tie ─────────
        // std::tie produces a std::tuple<...> whose operator!= is the
        // lexicographic compare of its elements.  Adding a new
        // FontSpec field is a two-edit change: extend the column
        // list here, and FontSpec::operator== in font_engine.hpp.
        // Callers do not need to be updated.  This is the same
        // pattern used in compile-text-layout's cache-key builder
        // and matches the Verdict-Issue-#3 stabilization contract.
        //
        // ── Why font_size is intentionally EXCLUDED ────────────
        // A single font may legitimately be shaped at different
        // sizes mid-paragraph: superscript, drop-cap scaling,
        // emphasis gradients (e.g. progressively larger characters
        // for typewriter-style intro animations).  These are
        // LAYOUT concerns, not FONT IDENTITY concerns, and a size
        // variation must NOT trip multi-font detection — the
        // renderer is expected to keep using the resolved font
        // (single BLFont) and only vary font_size at span
        // boundaries in a future atom.
        //
        // The compare fields (path / family / weight / style) are
        // the FontSpec fields that gate FONT IDENTITY — handled
        // here + in FontSpec::operator== in font_engine.hpp.
        if (std::tie(f.font_path,   f.font_family,   f.font_weight, f.font_style)
         != std::tie(first.font_path, first.font_family, first.font_weight, first.font_style)) {
            return true;
        }
    }
    return false;
}

} // namespace chronon3d
