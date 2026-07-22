#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_document.hpp — Canonical text document with rich spans
//
// TextDocument is the single source of truth for text content, styling, and
// paragraph structure.  It replaces ad-hoc per-character/animated-string
// solutions with a principled data model that separates:
//
//   1. Source text (utf8)          — what characters
//   2. Defaults (TextSpec)         — fallback font, size, appearance
//   3. TextStyleSpan[]             — per-range overrides (font, color, …)
//   4. ParagraphRange[]            — paragraph boundaries + paragraph styles
//
// The pipeline:
//
//   TextDocument
//       ↓
//   TextStyleResolver (resolve spans → per-run TextSpec)
//       ↓
//   Bidi segmentation + HarfBuzz shaping
//       ↓
//   ParagraphComposer (SingleLine or EveryLine)
//       ↓
//   TextRunLayout (immutable, shared)
//       ↓
//   TextAnimator stack → GlyphInstanceState → renderer
//
// PR 1 (this file) creates the data model + hash + validation.  The composer
// and resolver logic follow in PR 2+.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <chronon3d/text/text_appearance_spec.hpp>
#include <chronon3d/text/text_span_override.hpp>
#include <chronon3d/text/text_spec.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

// ── TextStyleSpan — applies styling overrides to a byte range ─────────────
//
// Each span overrides zero or more aspects of the text defaults for the
// UTF-8 byte range [byte_start, byte_end).  Spans MUST be non-overlapping
// and sorted by byte_start (enforced by TextDocument::validate()).
//
// A field that is std::nullopt means "inherit from defaults / enclosing span".
// Spans are resolved in document order; the LAST span covering a byte wins.

struct TextStyleSpan {
    size_t byte_start{0};   // inclusive
    size_t byte_end{0};     // exclusive

    std::optional<FontSpec>           font;
    std::optional<TextAppearanceSpec> appearance;
    std::optional<f32>               tracking;
    std::optional<f32>               baseline_shift;

    // FASE 4a: per-span identity + sizing multipliers (wired by TextDocumentBuilder).
    std::optional<std::string>       semantic_id;          ///< stable id for analytics/sync/highlights
    std::optional<f32>               font_size_multiplier;  ///< multiplier vs paragraph default (1.0 = default)

    [[nodiscard]] bool operator==(const TextStyleSpan&) const noexcept = default;
};

// ── ParagraphRange — byte range with associated paragraph style ────────────
//
// Paragraphs MUST cover the entire document (every byte belongs to exactly
// one paragraph) and MUST be non-overlapping + sorted by byte_start.
// Adjacent paragraph ranges with identical ParagraphStyle may be merged.

struct ParagraphRange {
    size_t byte_start{0};   // inclusive
    size_t byte_end{0};     // exclusive
    ParagraphStyle style{};

    [[nodiscard]] bool operator==(const ParagraphRange&) const noexcept = default;
};

// ── TextDocument — canonical text source ──────────────────────────────────

struct TextDocument {
    /// Raw UTF-8 text.  All byte offsets in spans/paragraphs refer to this
    /// string.  Must be valid UTF-8 (enforced by validate()).
    std::string utf8;

    /// Default font, layout, and appearance for the entire document.
    /// Spans override specific fields; anything not overridden falls back
    /// to these defaults.
    TextSpec defaults;

    /// Style overrides for specific byte ranges.
    /// Sorted by byte_start, non-overlapping.
    std::vector<TextStyleSpan> spans;

    /// Paragraph boundaries and styles.
    /// Sorted by byte_start, non-overlapping, covering [0, utf8.size()).
    /// If empty after construction, a single default ParagraphRange is
    /// created by split_paragraphs().
    std::vector<ParagraphRange> paragraphs;

    // ── Validation ──────────────────────────────────────────────────────

    /// Returns true if the document is well-formed:
    ///   - utf8 is valid UTF-8 (or empty)
    ///   - spans are sorted, non-overlapping, within [0, utf8.size())
    ///   - paragraphs are sorted, non-overlapping, covering [0, utf8.size())
    [[nodiscard]] bool validate() const;

    // ── Paragraph splitting ─────────────────────────────────────────────
    //
    /// Split the document into paragraphs on hard breaks (\n, \n\n, U+2029).
    /// If `paragraphs` is empty, this populates it with auto-detected ranges
    /// using `default_style`.  Otherwise it validates the existing ranges.
    /// Returns the number of paragraphs created (0 if already populated).
    size_t split_paragraphs(const ParagraphStyle& default_style = {});

    // Equality is intentionally omitted — use hash comparison for cache
    // keys and field-by-field checks in tests.
    // TextSpec does not expose `operator==`, so a defaulted `operator==`
    // on TextDocument would not compile when instantiated.
};

// ── Free functions ────────────────────────────────────────────────────────

/// Hash a TextDocument for cache keys.  Includes utf8 content, defaults,
/// spans, and paragraph metadata.
[[nodiscard]] u64 hash_text_document(const TextDocument& doc);

/// Hash a single TextStyleSpan.
[[nodiscard]] u64 hash_text_style_span(const TextStyleSpan& span);

/// Hash a ParagraphStyle.  Used by the layout cache to detect paragraph-level
/// changes that invalidate the composed layout.
[[nodiscard]] u64 hash_paragraph_style(const ParagraphStyle& ps);

/// Hash a ParagraphRange (byte range + style).
[[nodiscard]] u64 hash_paragraph_range(const ParagraphRange& pr);

/// Append a single TextSpanOverride to a TextDocument as a runtime
/// TextStyleSpan.  The override is merged on top of the supplied
/// default_font for font-size multiplier calculations.
void append_span_override(
    TextDocument& doc,
    const TextSpanOverride& over,
    const FontSpec& default_font);

} // namespace chronon3d
