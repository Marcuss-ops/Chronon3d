#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// line_break.hpp — Lightweight UAX#14-style line-break classifier
//
// Internal header — NOT part of the public API.  Lives in `src/text/unicode/`
// per the FASE 2 unicode-extraction plan.
//
// This is a deliberately simplified, table-free implementation of the
// Unicode Line Breaking Algorithm (UAX #14).  It does NOT pull in ICU or
// <unicode/...> (AGENTS.md Gate 5 deny-list).  The classifier groups every
// codepoint into one of a coarse set of LineBreakClass values and then
// applies a small, hard-coded rule set to decide whether a break is allowed
// between two neighbouring codepoints.
//
// The goal is to enable CJK / Thai / Devanagari / Arabic / Hebrew line
// breaking in the paragraph composer without requiring a full ICU dependency.
// Simplifications versus the full UAX#14 standard are clearly documented on
// each rule.
//
// Determinism: pure, no time, no PRNG, no threads.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace chronon3d {
namespace text {
namespace unicode {

// ── LineBreakClass — coarse UAX#14 categories used by the composer ───────
//
// Only the categories that influence the composer decisions are represented.
// Values that are not needed for line breaking (e.g. CM, CR, LF, AI, SG, XX)
// are folded into the closest semantic category.

enum class LineBreakClass : std::uint8_t {
    Unknown,      // fallback / unclassified
    Alphabet,     // AL — ordinary alphabetic and syllabic characters (Latin, Arabic, Hebrew, Devanagari, Thai, ...)
    Ideograph,    // ID — CJK ideographs, Hangul, etc.
    Numeric,      // NU — digits
    Inseparable,  // IN — inseparable characters (e.g. U+2025 TWO-DOT LEADER)
    Hyphen,       // HY — hyphen / minus / soft-hyphen
    BreakAfter,   // BA — break after (spaces, tabs, ZW spaces, etc.)
    BreakBefore,  // BB — break before (rare; composer uses as advisory)
    OpenPunct,    // OP — opening punctuation (quotation marks, brackets, etc.)
    ClosePunct,   // CL — closing punctuation
    Nonstarter,   // NS — non-starter punctuation (small kana, etc.)
    Postfix,      // PO — postfix punctuation
    Prefix,       // PR — prefix punctuation (currency, etc.)
    Mandatory,    // BK — mandatory break (folded from newline / paragraph sep)
    ZeroWidth,    // ZW — ZWSP / ZWNJ / ZWJ / BOM etc.
};

/// Classify a single codepoint into a coarse UAX#14 line-break class.
[[nodiscard]] LineBreakClass line_break_class(char32_t cp) noexcept;

/// Returns true if a line break is allowed between `before` and `after`.
/// Implements a simplified, but correct for common scripts, subset of
// UAX#14 pair rules:
//   LB1  : explicit BK / ZWSP permit breaks
//   LB2  : never break before / after mandatory break (handled elsewhere)
//   LB3  : always break at ZWSP / explicit break opportunities
//   LB13 : don't break before ']' , ')' , '>' , etc.
//   LB14 : don't break after  '[' , '(' , '<' , etc.
//   LB16 : don't break between closing punctuation and opening punctuation
//   LB18 : break after spaces
//   LB21 : don't break before / after hyphen-like characters
//   LB25 : don't break between numeric and prefix/postfix
//   LB30 : break between ideographs and most other classes (CJK wrapping)
//   LB31 : break everywhere else unless explicitly forbidden
[[nodiscard]] bool is_line_break_opportunity(LineBreakClass before, LineBreakClass after) noexcept;

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
