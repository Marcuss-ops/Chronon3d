// ═══════════════════════════════════════════════════════════════════════════
// line_break.cpp — Lightweight UAX#14-style line-break classifier impl
// ═══════════════════════════════════════════════════════════════════════════

#include "line_break.hpp"

namespace chronon3d {
namespace text {
namespace unicode {

namespace {

// ── Script / block helpers ───────────────────────────────────────────────

[[nodiscard]] inline constexpr bool in_range(char32_t cp, char32_t lo, char32_t hi) noexcept {
    return cp >= lo && cp <= hi;
}

[[nodiscard]] inline bool is_cjk_ideograph(char32_t cp) noexcept {
    // CJK Unified Ideographs + Extension A
    if (in_range(cp, 0x4E00, 0x9FFF)) return true;
    if (in_range(cp, 0x3400, 0x4DBF)) return true;
    if (in_range(cp, 0xF900, 0xFAFF)) return true;  // CJK compat ideographs
    if (in_range(cp, 0x20000, 0x2A6DF)) return true; // Extension B-D range start
    if (in_range(cp, 0x2A700, 0x2B73F)) return true; // Extension C-D
    if (in_range(cp, 0x2B740, 0x2B81F)) return true; // Extension E
    if (in_range(cp, 0x2B820, 0x2CEAF)) return true; // Extension F
    if (in_range(cp, 0x2CEB0, 0x2EBEF)) return true; // Extension G
    return false;
}

[[nodiscard]] inline bool is_hangul_syllable(char32_t cp) noexcept {
    return in_range(cp, 0xAC00, 0xD7AF);
}

[[nodiscard]] inline bool is_hiragana(char32_t cp) noexcept {
    return in_range(cp, 0x3040, 0x309F);
}

[[nodiscard]] inline bool is_katakana(char32_t cp) noexcept {
    return in_range(cp, 0x30A0, 0x30FF);
}

[[nodiscard]] inline bool is_thai(char32_t cp) noexcept {
    return in_range(cp, 0x0E00, 0x0E7F);
}

[[nodiscard]] inline bool is_lao(char32_t cp) noexcept {
    return in_range(cp, 0x0E80, 0x0EFF);
}

[[nodiscard]] inline bool is_devanagari(char32_t cp) noexcept {
    return in_range(cp, 0x0900, 0x097F);
}

[[nodiscard]] inline bool is_arabic(char32_t cp) noexcept {
    return in_range(cp, 0x0600, 0x06FF) ||
           in_range(cp, 0x0750, 0x077F) ||
           in_range(cp, 0x08A0, 0x08FF) ||
           in_range(cp, 0xFB50, 0xFDFF) ||
           in_range(cp, 0xFE70, 0xFEFF);
}

[[nodiscard]] inline bool is_hebrew(char32_t cp) noexcept {
    return in_range(cp, 0x0590, 0x05FF);
}

[[nodiscard]] inline bool is_common_digit(char32_t cp) noexcept {
    return in_range(cp, U'0', U'9') ||
           in_range(cp, 0x0660, 0x0669) ||  // Arabic-Indic
           in_range(cp, 0x06F0, 0x06F9) ||  // Extended Arabic-Indic
           in_range(cp, 0x0966, 0x096F) ||  // Devanagari
           in_range(cp, 0x0E50, 0x0E59) ||  // Thai
           in_range(cp, 0x0ED0, 0x0ED9) ||  // Lao
           in_range(cp, 0xFF10, 0xFF19);     // Fullwidth
}

[[nodiscard]] inline bool is_open_punct(char32_t cp) noexcept {
    switch (cp) {
    case U'(': case U'[': case U'{':
    case U'\u2018': case U'\u201C': case U'\u00AB': case U'\u2039':
    case U'\u3008': case U'\u300A': case U'\u300C': case U'\u300E':
    case U'\u3010': case U'\u3014': case U'\uFF08': case U'\uFF3B':
    case U'\uFF5B': case U'\uFF5F':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_close_punct(char32_t cp) noexcept {
    switch (cp) {
    case U')': case U']': case U'}':
    case U'\u2019': case U'\u201D': case U'\u00BB': case U'\u203A':
    case U'\u3009': case U'\u300B': case U'\u300D': case U'\u300F':
    case U'\u3011': case U'\u3015': case U'\uFF09': case U'\uFF3D':
    case U'\uFF5D': case U'\uFF60':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_hyphen_like(char32_t cp) noexcept {
    switch (cp) {
    case U'-':
    case U'\u2010': case U'\u2011': case U'\u2012':
    case U'\u2013': case U'\u2014':
    case U'\u00AD':  // soft hyphen
    case U'\uFE63':
    case U'\uFF0D':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_prefix_punct(char32_t cp) noexcept {
    switch (cp) {
    case U'$': case U'\u00A3': case U'\u00A5': case U'\u20AC':
    case U'\u00A2': case U'\u00A4':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_postfix_punct(char32_t cp) noexcept {
    switch (cp) {
    case U'%': case U'\u2030': case U'\u2031':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_nonstarter(char32_t cp) noexcept {
    // Simplified NS set: small kana that should not start a line.
    switch (cp) {
    // Hiragana small vowels
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
    case 0x3063:  // small TU
    case 0x3083: case 0x3085: case 0x3087: case 0x308E:
    // Katakana small vowels
    case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
    case 0x30C3:  // small TU
    case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:
    // Iteration marks
    case 0x309D: case 0x309E: case 0x30FD: case 0x30FE:
        return true;
    default:
        return false;
    }
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

LineBreakClass line_break_class(char32_t cp) noexcept {
    if (cp == U'\n' || cp == U'\r' || cp == U'\u2029') {
        return LineBreakClass::Mandatory;
    }
    if (cp == U' ' || cp == U'\t' || cp == U'\u200B' || cp == U'\uFEFF') {
        return LineBreakClass::BreakAfter;  // includes ZWSP
    }
    if (cp == U'\u200D') {  // ZWJ
        return LineBreakClass::ZeroWidth;
    }
    if (cp == U'\u2011') {  // non-breaking hyphen
        return LineBreakClass::Alphabet;
    }
    if (is_open_punct(cp)) {
        return LineBreakClass::OpenPunct;
    }
    if (is_close_punct(cp)) {
        return LineBreakClass::ClosePunct;
    }
    if (is_hyphen_like(cp)) {
        return LineBreakClass::Hyphen;
    }
    if (is_prefix_punct(cp)) {
        return LineBreakClass::Prefix;
    }
    if (is_postfix_punct(cp)) {
        return LineBreakClass::Postfix;
    }
    if (is_common_digit(cp)) {
        return LineBreakClass::Numeric;
    }
    if (is_cjk_ideograph(cp) || is_hangul_syllable(cp) || is_hiragana(cp) || is_katakana(cp)) {
        return LineBreakClass::Ideograph;
    }
    if (is_nonstarter(cp)) {
        return LineBreakClass::Nonstarter;
    }
    // Thai, Lao, Devanagari: treat as ideograph-like for breaking so
    // text in these scripts wraps even when no spaces are present.
    if (is_thai(cp) || is_lao(cp) || is_devanagari(cp)) {
        return LineBreakClass::Ideograph;
    }
    if (is_arabic(cp) || is_hebrew(cp)) {
        return LineBreakClass::Alphabet;
    }

    // Latin, Greek, Cyrillic and everything else defaults to AL.
    return LineBreakClass::Alphabet;
}

bool is_line_break_opportunity(LineBreakClass before, LineBreakClass after) noexcept {
    // LB2 / BK handling is done at a higher level (mandatory break).
    // ZWSP / ZW handling: explicit break opportunity.  Only break AFTER a
    // space/ZWSP; breaking before a space would leave whitespace at the
    // start of the next line.
    if (before == LineBreakClass::BreakAfter) {
        return true;
    }

    // LB14: don't break after opening punctuation.
    if (before == LineBreakClass::OpenPunct) {
        return false;
    }
    // LB13: don't break before closing punctuation.
    if (after == LineBreakClass::ClosePunct) {
        return false;
    }
    // LB16: don't break between closing and opening punctuation.
    if (before == LineBreakClass::ClosePunct && after == LineBreakClass::OpenPunct) {
        return false;
    }
    // LB21: don't break before / after hyphen-like characters.
    if (before == LineBreakClass::Hyphen || after == LineBreakClass::Hyphen) {
        return false;
    }
    // LB25 / numeric prefix / postfix keep together.
    if (before == LineBreakClass::Numeric &&
        (after == LineBreakClass::Prefix || after == LineBreakClass::Postfix)) {
        return false;
    }
    if ((before == LineBreakClass::Prefix || before == LineBreakClass::Postfix) &&
        after == LineBreakClass::Numeric) {
        return false;
    }
    // NS should not end a line.
    if (before == LineBreakClass::Nonstarter) {
        return false;
    }
    // PR should not end a line.
    if (before == LineBreakClass::Prefix) {
        return false;
    }
    // PO should not begin a line.
    if (after == LineBreakClass::Postfix) {
        return false;
    }

    // LB30: break between ideographs and most other classes (CJK wrapping).
    if (before == LineBreakClass::Ideograph || after == LineBreakClass::Ideograph) {
        return true;
    }

    // LB31: alphabet-alphabet does NOT break (whitespace is handled above).
    return false;
}

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
