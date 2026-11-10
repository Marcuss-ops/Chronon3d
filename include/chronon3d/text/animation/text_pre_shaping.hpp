#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_pre_shaping.hpp — PreShaping property evaluation (FASE 2a)
//
// Evaluates TextAnimatorProperty variants whose PropertyPhase is
// PreShaping BEFORE HarfBuzz shaping: CharacterOffsetProperty and
// CharacterValueProperty.  Further text substitutions will be added here.
//
// Pipeline:
//   Source text → PreShaping evaluator → HarfBuzz shaping → Layout
//
// When any PreShaping property produces a different source text, the
// caller MUST rebuild the layout (invalidate shaping + layout cache).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/text/glyph_selector.hpp>

#include <chronon3d/core/types/sample_time.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ── apply_character_offset_to_source ────────────────────────────────────
// Apply a Caesar-cipher code-point offset to a UTF-8 string.
//
// Each Unicode code point in the Basic Latin / Latin-1 Supplement
// uppercase (A-Z) and lowercase (a-z) ranges is shifted by `offset`
// positions.  Non-alphabetic code points (digits, spaces, punctuation,
// multi-byte CJK, emoji, RTL, etc.) pass through unchanged.
//
// Wrap behaviour matches After Effects Character Offset:
//   offset=1: A→B,  Z→A,  a→b,  z→a
//   offset=2: A→C,  Y→A,  Z→B
//
// The output string always has the same byte length as the input
// (only single-byte ASCII letters are modified).

[[nodiscard]] std::string apply_character_offset_to_source(
    std::string_view source,
    i32 offset
);

// ── apply_character_value_to_source ─────────────────────────────────────
// Replace every non-whitespace code point with `value` (After Effects
// Character Value).
//
// ASCII whitespace (space, tab, CR, LF, FF, VT) passes through unchanged so
// line breaks and word boundaries survive the effect.  When `value` is not a
// valid Unicode scalar (0, a surrogate, or > U+10FFFF) the source is returned
// unchanged.

[[nodiscard]] std::string apply_character_value_to_source(
    std::string_view source,
    char32_t value
);

// ── has_pre_shaping_properties ──────────────────────────────────────────
// Returns true if any animator in the stack contains a PreShaping property.

[[nodiscard]] bool has_pre_shaping_properties(
    const std::vector<TextAnimatorSpec>& animators
);

// ── ADR-037 — canonical pre-shaping unit map ────────────────────────────
// A source-codepoint view of the authored layout.  `proxy` carries one synthetic
// glyph per source codepoint so the existing selector compiler/evaluator can
// resolve weights without a second implementation; `units` is that proxy's
// canonical GlyphSelectorUnitMap.  `codepoints` keeps the byte range and the
// grapheme/word/line indices used by the fold.

struct PreShapingCodepoint {
    std::size_t byte_offset{0};
    std::size_t byte_len{0};
    u32 grapheme_index{0};
    u32 word_index{std::numeric_limits<u32>::max()};
    u32 line_index{0};
    std::string semantic_id;
};

struct PreShapingUnitMap {
    std::string source;      // authored UTF-8 source
    std::string font_key;    // layout/font identity used to build `proxy`
    PlacedGlyphRun proxy;    // one synthetic glyph per source codepoint
    GlyphSelectorUnitMap units;
    std::vector<PreShapingCodepoint> codepoints;
    std::vector<u32> first_codepoint_for_grapheme;

    [[nodiscard]] std::size_t codepoint_count() const noexcept {
        return codepoints.size();
    }
};

// Build the canonical map from the shaped authored source.  Clusters that
// HarfBuzz collapses (ligatures, combining marks) stay one grapheme unit; spaces
// omitted from the glyph stream are restored from the source walk.  The proxy's
// `y` values preserve wrapped-line geometry so word/line units match the
// PostLayout selector map.
[[nodiscard]] std::shared_ptr<const PreShapingUnitMap> build_pre_shaping_unit_map(
    const PlacedGlyphRun& canonical,
    std::string_view source,
    std::string font_key = {}
);

// True when at least one enabled animator has both selectors and a PreShaping
// property: only then does the driver pay for the canonical map.
[[nodiscard]] bool has_selector_scoped_pre_shaping(
    const std::vector<TextAnimatorSpec>& animators
);

// ── evaluate_pre_shaping_source ─────────────────────────────────────────
// Scan the animator stack for PreShaping properties and apply them to
// the source text.  Returns the transformed source text.
//
// Handles CharacterOffsetProperty (offsets sum across animators) and
// CharacterValueProperty (the last valid sampled scalar wins), in that order.
//
// The caller must compare the returned string against the original
// source_text and rebuild the layout when they differ.

[[nodiscard]] std::string evaluate_pre_shaping_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source,
    SampleTime time = {}
);

// ADR-037 selector-scoped overload.  When `unit_map` is non-null, each animator
// resolves its selectors against the canonical map and applies its PreShaping
// properties only to the selected codepoints, in stack order.  A null map keeps
// the historical run-level behaviour.
[[nodiscard]] std::string evaluate_pre_shaping_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source,
    SampleTime time,
    const PreShapingUnitMap* unit_map
);

} // namespace chronon3d
