#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_error.hpp — Structured error type for text content helpers
//
// F0.3 — Content-side error taxonomy.  Parallel to TextLayoutError
// (compiler-side, in text_run_builder.hpp) but scoped to the higher-level
// helpers in content/text/.  Every silent return in the typewriter/
// centering/glow helpers now surfaces a TextError with a programmatic
// TextErrorCode so callers can branch on the failure reason instead of
// treating all failures as opaque.
//
// Naming: TextErrorCode (enum) + TextError (struct) mirror the
// compiler-side TextLayoutErrorKind + TextLayoutError pattern exactly.
// ═══════════════════════════════════════════════════════════════════════════

#include <string>

namespace chronon3d {

/// Content-side error taxonomy for text helpers (typewriter, centering,
/// glow, etc.).  Each variant maps to a specific silent-return path
/// that previously returned zero-initialized layout data with no
/// diagnostic.
enum class TextErrorCode {
    /// No FontEngine* provided to the helper.  Distinct from the
    /// compiler-side MissingFontEngine (TextLayoutErrorKind) — this
    /// is a content-helper pre-condition, not a compiler invariant.
    MissingFontEngine,

    /// Source text string is empty (no UTF-8 bytes).  Caller
    /// forgot to set .text on the options struct.
    EmptyText,

    /// HarfBuzz shaping returned zero glyphs for a non-empty text
    /// input.  Font may be missing, corrupt, or the input contains
    /// only non-renderable codepoints.
    ShapingFailed,

    /// PlacedGlyphRun has zero clusters after resolve_placed_glyph_run().
    /// Indicates a mismatch between shaped glyphs and cluster mapping.
    NoClusters,

    /// Word-wrap produced zero lines for a non-empty text with a
    /// valid box width.  Typically means the box width is smaller
    /// than the widest character.
    NoLayoutLines,

    /// TypewriterLayout::chars is empty after layout computation.
    /// Propagated from compute_typewriter_layout() returning Ok but
    /// with zero characters — a belt-and-suspenders guard.
    NoLayoutChars,
};

/// Structured error for text content helpers.
/// `code` gives callers programmatic switching; `message` carries a
/// one-line diagnostic for logging/debug.
struct TextError {
    TextErrorCode code{TextErrorCode::EmptyText};
    std::string   message{};
};

} // namespace chronon3d
