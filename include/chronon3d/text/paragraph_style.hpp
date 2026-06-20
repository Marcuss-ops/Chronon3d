#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// paragraph_style.hpp — Paragraph-level typography descriptors
//
// These types live in the text pipeline between TextDocument and the
// ParagraphComposer (SingleLine / EveryLine).  They describe HOW the composer
// should break and justify a paragraph, but do not contain the composer
// algorithm itself (that lives in the composer files, PR 2+).
//
// ParagraphStyle is referenced by ParagraphRange inside TextDocument, so it
// is defined here rather than in a composer-specific header.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>

namespace chronon3d {

// ── ParagraphComposer — which line-breaking algorithm to use ──────────────

enum class ParagraphComposer : u8 {
    /// Greedy single-line-at-a-time (O(n)).  Fast and predictable;
    /// ideal for subtitles, lower-thirds, and real-time text updates.
    SingleLine,

    /// Global optimisation across all lines (dynamic programming,
    /// simplified Knuth-Plass).  Produces visually balanced paragraphs
    /// but is O(n²).  Best for static title cards and pre-rendered text.
    EveryLine,
};

// ── TextJustification — horizontal alignment mode ────────────────────────

enum class TextJustification : u8 {
    Left,
    Center,
    Right,
    Full,                // all lines fully justified
    FullLastLineLeft,    // last line left-aligned
    FullLastLineCenter,  // last line centered
    FullLastLineRight,   // last line right-aligned
};

// ── ParagraphSpacing — justification stretch/shrink limits ──────────────
//
// Controls how much the composer can adjust word spacing, letter spacing,
// and horizontal glyph scale to fit a line to the target width.  All values
// are ratios relative to the natural spacing / size.

struct ParagraphSpacing {
    f32 word_min{0.80f};       // minimum word-spacing multiplier
    f32 word_desired{1.00f};   // desired word-spacing multiplier (natural)
    f32 word_max{1.33f};       // maximum word-spacing multiplier

    f32 letter_min{-0.02f};    // minimum letter-spacing addition (px)
    f32 letter_desired{0.0f};  // desired letter-spacing (px)
    f32 letter_max{0.05f};     // maximum letter-spacing addition (px)

    f32 glyph_scale_min{0.97f};     // minimum horizontal glyph scale
    f32 glyph_scale_desired{1.0f};  // desired scale (natural)
    f32 glyph_scale_max{1.03f};     // maximum horizontal glyph scale
};

// ── ParagraphSpacingCollapse — how adjacent space_before/space_after merge ──

enum class ParagraphSpacingCollapse : u8 {
    /// Add both values (20px after + 30px before = 50px gap).
    Add,

    /// Take the maximum (20px after + 30px before = 30px gap).
    Maximum,
};

// ── ParagraphStyle — controls layout of a single paragraph ───────────────

struct ParagraphStyle {
    // ── Indentation ─────────────────────────────────────────────────────
    f32 left_indent{0.0f};
    f32 right_indent{0.0f};
    /// First-line indent.  Can be negative for hanging indent
    /// (use with positive left_indent, e.g. left=30, first_line=-30).
    f32 first_line_indent{0.0f};

    // ── Vertical spacing ────────────────────────────────────────────────
    f32 space_before{0.0f};   // vertical space before this paragraph
    f32 space_after{0.0f};    // vertical space after this paragraph

    // ── Composer & justification ────────────────────────────────────────
    ParagraphComposer composer{ParagraphComposer::SingleLine};
    TextJustification justification{TextJustification::Left};
    ParagraphSpacing spacing{};

    // ── Hanging punctuation ─────────────────────────────────────────────
    /// When enabled, quotation marks and certain punctuation may extend
    /// slightly past the margin for better optical alignment.
    bool hanging_punctuation{false};
    /// Fraction of glyph width allowed to overhang (0.0 = none, 0.5 = half).
    f32 hanging_limit{0.5f};

    // ── Hyphenation ─────────────────────────────────────────────────────
    bool hyphenation{false};
    int minimum_word_length{6};   // don't hyphenate words shorter than this
    int minimum_prefix{3};        // minimum characters before the hyphen
    int minimum_suffix{3};        // minimum characters after the hyphen

    // ── Widow/orphan control ────────────────────────────────────────────
    /// Minimum number of lines allowed at the top of a column (widow).
    /// 0 = no control.
    int widow_lines{1};
    /// Minimum number of lines allowed at the bottom of a column (orphan).
    /// 0 = no control.
    int orphan_lines{1};

    // ── Spacing collapse ────────────────────────────────────────────────
    /// How adjacent space_before/space_after values merge across paragraph
    /// boundaries.  e.g. paragraph A ends with space_after=20, paragraph B
    /// starts with space_before=30.  With Add: 50px.  With Maximum: 30px.
    ParagraphSpacingCollapse spacing_collapse{ParagraphSpacingCollapse::Maximum};

    // ── Equality ────────────────────────────────────────────────────────
    bool operator==(const ParagraphStyle&) const noexcept = default;
};

} // namespace chronon3d
