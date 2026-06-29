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
//
// ── TEXT-PLY-01 (env-vars Agent3) ─────────────────────────────────────────
// 14-feature extension over the PR-2/3 baseline.  All new fields have
// default values that preserve the existing == operator semantics; the
// existing 38+ callers + tests are unchanged.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/text_direction.hpp>  // TextDirection

#include <string>
#include <vector>

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

    bool operator==(const ParagraphSpacing&) const noexcept = default;
};

// ── ParagraphSpacingCollapse — how adjacent space_before/space_after merge ──

enum class ParagraphSpacingCollapse : u8 {
    /// Add both values (20px after + 30px before = 50px gap).
    Add,

    /// Take the maximum (20px after + 30px before = 30px gap).
    Maximum,
};

// ── VariableAxis — OpenType font variation axis descriptor ────────────────

struct VariableAxis {
    /// OpenType axis tag (4 ASCII chars, e.g. "wght", "wdth", "slnt", "ital").
    std::string tag{};
    /// Axis value in axis-defined units (typically 0..1000 or -100..100).
    /// Default 0.0; consumer code interprets -1.0 sentinel as "inherit".
    f32 value{0.0f};

    bool operator==(const VariableAxis&) const noexcept = default;
};

// ── ParagraphMarkGlyph — decorative glyph rendered before/after a paragraph ─

enum class ParagraphMarkGlyph : u8 {
    None,    // no paragraph mark
    Bullet,  // U+2022 BULLET
    EnDash,  // U+2013 EN DASH
    EmDash,  // U+2014 EM DASH
    Diamond, // U+25C6 BLACK DIAMOND
    Custom,  // uses paragraph_mark_custom field (UTF-8 string)
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

    // ── Direction ───────────────────────────────────────────────────────
    /// Explicit paragraph base direction.  When set to LTR or RTL, all
    /// text runs within this paragraph use the specified direction
    /// regardless of their content (overrides bidi auto-detection).
    /// Default: Auto (bidi-detect from text content).
    TextDirection direction{TextDirection::Auto};

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
    /// The rule is honoured by `text_run_builder.cpp`'s per-document
    /// paragraph pipeline (wired in TEXT-PLY-01 via apply_spacing_collapse).
    ParagraphSpacingCollapse spacing_collapse{ParagraphSpacingCollapse::Maximum};

    // ════════════════════════════════════════════════════════════════════
    // ── TEXT-PLY-01: 14-feature extension (env-vars Agent3) ─────────────
    // ════════════════════════════════════════════════════════════════════

    // 1. language — BCP-47 shaping language hint (e.g. "en", "ja", "zh-Hans").
    /// Empty string = inherit from upstream (spec / engine default).
    std::string language{};

    // 2. feature_tags — OpenType feature toggles (e.g. "liga", "dlig", "kern", "smcp").
    /// Empty vector = engine default feature set.  Values are forward-declared
    /// by FontEngine; unknown tags are silently ignored.
    std::vector<std::string> feature_tags{};

    // 3. variable_axes — OpenType font variation (e.g. wght=350, wdth=120, slnt=-8).
    /// Empty vector = inherit engine axis defaults.
    std::vector<VariableAxis> variable_axes{};

    // 4. tab_stops — explicit tab stop positions (px from left edge of paragraph).
    /// Empty vector = no explicit tabs; tab cluster is treated as whitespace
    /// (allowed_break_after=true).  When non-empty, tab clusters snap to the
    /// next tab stop ≥ current x position (downstream renderer responsibility).
    std::vector<f32> tab_stops{};

    // 5. kinsoku — CJK break-protection rules (simplified).
    /// When true, certain CJK opening brackets (「『（【《〈) prevent line
    /// breaks immediately after them — matches the "hang-as-end-of-line"
    /// rule used by After Effects and Adobe InDesign CJK options.
    /// Closing-bracket no-break-before rule is intentionally left to a
    /// follow-up atom (would require extending ShapedCluster with a
    /// no_break_before flag).
    bool kinsoku{false};

    // 6. auto_fit_font_size cluster — auto-shrink font size to fit.
    /// When true, downstream renderer may reduce the effective font size
    /// to make the paragraph fit the available box.  min_font_size and
    /// max_font_size clamp the search range.
    bool auto_fit_font_size{false};
    f32  min_font_size{8.0f};
    f32  max_font_size{96.0f};

    // 7. discretionary_ligatures — opt-in for the OpenType `dlig` feature.
    /// Default false.  When true, ligatures not part of the standard
    /// `liga` set are applied.  Used for decorative typography (expert
    /// fonts, italics).
    bool discretionary_ligatures{false};

    // 8. drop_cap_height — number of lines reserved for a drop cap.
    /// 0 = no drop cap.  When >0, downstream renderer reserves the first
    /// drop_cap_height × line-height vertical units and renders an
    /// oversized initial letter within that region.
    int drop_cap_height{0};

    // 9. strict_widow_orphan — cascade fixup.
    /// When true, widow/orphan enforcement runs in a loop until either the
    /// constraints are satisfied or no further merge is possible (capped
    /// at 16 passes to prevent pathological cases).  Default false matches
    /// the prior single-pass behaviour.
    bool strict_widow_orphan{false};

    // 11. tight_leading — negative leading multiplier [0.0, 1.0).
    /// -1.0 sentinel = inherit (use spec / engine default).  When in [0,1),
    /// the effective line height shrinks: e.g. 0.85 = 15% tighter than the
    /// standard ascent+descent.  Applied in finalize_lines modify-step.
    f32 tight_leading{-1.0f};

    // 12. hyphenation_lang — BCP-47 override for hyphenation dictionary lookup.
    /// Empty string = inherit (use `language` field).  Independent so
    /// a Spanish-language shape can hyphenate per English rules.
    std::string hyphenation_lang{};

    // 13. justification_tolerance_px — absolute pixel cap on per-line spread.
    /// 0.0 default = no clamp (back-compat with PR-2/3 behaviour).
    /// Justification `delta` is clamped to ±tolerance_px before being
    /// distributed across word/letter/glyph adjustments.  Use 4.0 to limit
    /// per-glyph drift to sub-pixel rounding.
    f32 justification_tolerance_px{0.0f};

    // 14. paragraph_mark — decorative glyph rendered before/after the paragraph.
    /// Default None.  Custom uses paragraph_mark_custom (UTF-8 string).
    ParagraphMarkGlyph paragraph_mark{ParagraphMarkGlyph::None};
    std::string       paragraph_mark_custom{};

    // ── Equality ────────────────────────────────────────────────────────
    bool operator==(const ParagraphStyle&) const noexcept = default;
};

} // namespace chronon3d
