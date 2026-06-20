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
//       ↓  split_paragraphs()         ← already in text_document.cpp
//       ↓  resolve_fallback_fonts()    ← font substitution when primary missing
//       ↓  resolve_text_run_tree()     ← spans → bidi → resolved runs
//       ↓  ParagraphComposer + HarfBuzz (PR 7+)
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

} // namespace chronon3d
