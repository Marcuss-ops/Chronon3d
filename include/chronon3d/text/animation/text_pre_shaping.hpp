#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_pre_shaping.hpp — PreShaping property evaluation (FASE 2a)
//
// Evaluates TextAnimatorProperty variants whose PropertyPhase is
// PreShaping BEFORE HarfBuzz shaping.  Currently only
// CharacterOffsetProperty is PreShaping (CharacterValue and text
// substitutions will be added here later).
//
// Pipeline:
//   Source text → PreShaping evaluator → HarfBuzz shaping → Layout
//
// When any PreShaping property produces a different source text, the
// caller MUST rebuild the layout (invalidate shaping + layout cache).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

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

// ── has_pre_shaping_properties ──────────────────────────────────────────
// Returns true if any animator in the stack contains a PreShaping property.

[[nodiscard]] bool has_pre_shaping_properties(
    const std::vector<TextAnimatorSpec>& animators
);

// ── evaluate_pre_shaping_source ─────────────────────────────────────────
// Scan the animator stack for PreShaping properties and apply them to
// the source text.  Returns the transformed source text.
//
// Currently handles CharacterOffsetProperty.  When multiple animators
// have CharacterOffset properties, they are combined additively.
//
// The caller must compare the returned string against the original
// source_text and rebuild the layout when they differ.

[[nodiscard]] std::string evaluate_pre_shaping_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source
);

} // namespace chronon3d
