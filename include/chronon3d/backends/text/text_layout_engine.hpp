#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

// ── UTF-8 code-point helpers ───────────────────────────────────────
// Used to make wrapping / ellipsis / splitting grapheme-aware instead
// of byte-aware, preventing broken UTF-8 sequences for non-Latin text.

namespace detail {

/// Return the byte length of the UTF-8 sequence starting at the given byte.
/// Returns 1 for ASCII, 2–4 for multi-byte sequences, 1 for invalid.
inline size_t utf8_seq_len(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1; // invalid continuation byte — skip one
}

/// Decode a single UTF-8 code point at the given position.
/// Advances `pos` by the sequence length.  Returns U+FFFD on error.
inline char32_t utf8_decode(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return 0;
    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    const size_t len = utf8_seq_len(c0);
    if (pos + len > s.size()) { ++pos; return 0xFFFD; }
    char32_t cp;
    switch (len) {
        case 1: cp = c0; break;
        case 2: cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[pos+1]) & 0x3F); break;
        case 3: cp = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[pos+1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos+2]) & 0x3F); break;
        case 4: cp = ((c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[pos+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[pos+2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos+3]) & 0x3F); break;
        default: cp = 0xFFFD; break;
    }
    pos += len;
    return cp;
}

/// Remove the last code point from a UTF-8 string (in-place).
/// Safe for multi-byte sequences — scans backwards from end.
inline void utf8_pop_back(std::string& s) {
    if (s.empty()) return;
    size_t pos = s.size() - 1;
    // Skip continuation bytes (10xxxxxx)
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    s.resize(pos);
}

/// Return the number of code points in a UTF-8 string (O(n)).
inline size_t utf8_length(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size();) {
        i += utf8_seq_len(static_cast<unsigned char>(s[i]));
        ++count;
    }
    return count;
}

/// Overload for std::string_view — avoids copying into std::string.
inline size_t utf8_length(std::string_view sv) {
    size_t count = 0;
    for (size_t i = 0; i < sv.size();) {
        i += utf8_seq_len(static_cast<unsigned char>(sv[i]));
        ++count;
    }
    return count;
}

// ── Grapheme cluster segmentation (UAX #29 simplified) ───────────
//
// These functions iterate by *extended grapheme clusters* —
// user-perceived characters — instead of raw code points.
// A grapheme cluster keeps combining marks, ZWJ emoji sequences,
// emoji skin-tone modifiers, and regional-indicator flag pairs
// together so they are never split mid-character during animations.
//
// Extended grapheme clusters are defined by Unicode Standard Annex #29.
// We implement the core rules GB1–GB5, GB9–GB11, and GB12–GB13
// using static range tables instead of ICU, keeping this header
// dependency-free while covering the most common real-world scripts.

/// Decode a single UTF-8 code point at the current offset.
/// Advances `offset` by the sequence length.  Returns U+FFFD on error.
/// `string_view` overload for use with `std::string_view` callers
/// (the existing `utf8_decode` takes `const std::string&`).
inline char32_t utf8_decode_cp(std::string_view sv, size_t& offset) {
    if (offset >= sv.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(sv[offset]);
    const size_t len = utf8_seq_len(lead);
    if (offset + len > sv.size()) { ++offset; return 0xFFFD; }

    char32_t cp;
    switch (len) {
        case 1: cp = lead; break;
        case 2: cp = ((lead & 0x1F) << 6) | (static_cast<unsigned char>(sv[offset+1]) & 0x3F); break;
        case 3: cp = ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(sv[offset+1]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset+2]) & 0x3F); break;
        case 4: cp = ((lead & 0x07) << 18) | ((static_cast<unsigned char>(sv[offset+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(sv[offset+2]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset+3]) & 0x3F); break;
        default: cp = 0xFFFD; break;
    }
    offset += len;
    return cp;
}

/// True if `cp` is a Grapheme_Extend character (continues the current
/// cluster).  Covers combining marks (Mn/Mc/Me), ZWJ, variation
/// selectors, emoji modifiers, and other invisible extenders.
inline bool is_grapheme_extend(char32_t cp) noexcept {
    // Zero-width joiner — U+200D
    if (cp == 0x200D) return true;

    // ── Combining Diacritical Marks: U+0300–U+036F ─────────────
    if (cp >= 0x0300 && cp <= 0x036F) return true;

    // ── Combining Diacritical Marks Extended: U+1AB0–U+1AFF ────
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;

    // ── Combining Diacritical Marks Supplement: U+1DC0–U+1DFF ──
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;

    // ── Combining Diacritical Marks for Symbols: U+20D0–U+20FF ─
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;

    // ── Combining Half Marks: U+FE20–U+FE2F ────────────────────
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;

    // ── Variation Selectors: U+FE00–U+FE0F ─────────────────────
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    // Variation Selectors Supplement: U+E0100–U+E01EF
    if (cp >= 0xE0100 && cp <= 0xE01EF) return true;

    // ── Emoji modifiers (skin tones): U+1F3FB–U+1F3FF ──────────
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true;

    // ── Arabic combining marks ──────────────────────────────────
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp == 0x0670) return true;
    if (cp >= 0x06D6 && cp <= 0x06DC) return true;
    if (cp >= 0x06DF && cp <= 0x06E4) return true;
    if (cp >= 0x06E7 && cp <= 0x06E8) return true;
    if (cp >= 0x06EA && cp <= 0x06ED) return true;
    if (cp >= 0x08D4 && cp <= 0x08E1) return true;
    if (cp >= 0x08E3 && cp <= 0x08FF) return true;

    // ── Cyrillic combining marks ────────────────────────────────
    if (cp >= 0x0483 && cp <= 0x0489) return true;

    // ── Hebrew combining marks ──────────────────────────────────
    if (cp >= 0x0591 && cp <= 0x05BD) return true;
    if (cp == 0x05BF) return true;
    if (cp >= 0x05C1 && cp <= 0x05C2) return true;
    if (cp >= 0x05C4 && cp <= 0x05C5) return true;
    if (cp == 0x05C7) return true;

    // ── Syriac combining marks ──────────────────────────────────
    if (cp == 0x0711) return true;
    if (cp >= 0x0730 && cp <= 0x074A) return true;

    // ── Thaana combining marks ──────────────────────────────────
    if (cp >= 0x07A6 && cp <= 0x07B0) return true;

    // ── N'Ko combining marks ────────────────────────────────────
    if (cp >= 0x07EB && cp <= 0x07F3) return true;
    if (cp >= 0x07FD && cp <= 0x07FD) return true;

    // ── Samaritan combining marks ───────────────────────────────
    if (cp >= 0x0816 && cp <= 0x0819) return true;
    if (cp >= 0x081B && cp <= 0x0823) return true;
    if (cp >= 0x0825 && cp <= 0x0827) return true;
    if (cp >= 0x0829 && cp <= 0x082D) return true;

    // ── Mandaic combining marks ─────────────────────────────────
    if (cp >= 0x0859 && cp <= 0x085B) return true;

    // ── Devanagari + other Indic combining marks ────────────────
    // Devanagari
    if (cp >= 0x0900 && cp <= 0x0903) return true;
    if (cp >= 0x093A && cp <= 0x093C) return true;
    if (cp >= 0x093E && cp <= 0x094F) return true;
    if (cp >= 0x0951 && cp <= 0x0957) return true;
    if (cp >= 0x0962 && cp <= 0x0963) return true;
    // Bengali
    if (cp >= 0x0981 && cp <= 0x0983) return true;
    if (cp >= 0x09BC && cp <= 0x09BC) return true;
    if (cp >= 0x09BE && cp <= 0x09C4) return true;
    if (cp >= 0x09C7 && cp <= 0x09C8) return true;
    if (cp >= 0x09CB && cp <= 0x09CD) return true;
    if (cp == 0x09D7) return true;
    if (cp >= 0x09E2 && cp <= 0x09E3) return true;
    if (cp == 0x09FE) return true;
    // Gurmukhi
    if (cp >= 0x0A01 && cp <= 0x0A03) return true;
    if (cp == 0x0A3C) return true;
    if (cp >= 0x0A3E && cp <= 0x0A42) return true;
    if (cp >= 0x0A47 && cp <= 0x0A48) return true;
    if (cp >= 0x0A4B && cp <= 0x0A4D) return true;
    if (cp == 0x0A51) return true;
    if (cp >= 0x0A70 && cp <= 0x0A71) return true;
    if (cp == 0x0A75) return true;
    // Gujarati
    if (cp >= 0x0A81 && cp <= 0x0A83) return true;
    if (cp == 0x0ABC) return true;
    if (cp >= 0x0ABE && cp <= 0x0AC5) return true;
    if (cp >= 0x0AC7 && cp <= 0x0AC9) return true;
    if (cp >= 0x0ACB && cp <= 0x0ACD) return true;
    if (cp >= 0x0AE2 && cp <= 0x0AE3) return true;
    // Oriya
    if (cp >= 0x0B01 && cp <= 0x0B03) return true;
    if (cp == 0x0B3C) return true;
    if (cp >= 0x0B3E && cp <= 0x0B44) return true;
    if (cp >= 0x0B47 && cp <= 0x0B48) return true;
    if (cp >= 0x0B4B && cp <= 0x0B4D) return true;
    if (cp >= 0x0B55 && cp <= 0x0B57) return true;
    if (cp >= 0x0B62 && cp <= 0x0B63) return true;
    // Tamil, Telugu, Kannada, Malayalam
    if (cp >= 0x0BBE && cp <= 0x0BC2) return true;
    if (cp >= 0x0BC6 && cp <= 0x0BC8) return true;
    if (cp >= 0x0BCA && cp <= 0x0BCD) return true;
    if (cp == 0x0BD7) return true;
    if (cp >= 0x0C00 && cp <= 0x0C04) return true;
    if (cp >= 0x0C3E && cp <= 0x0C44) return true;
    if (cp >= 0x0C46 && cp <= 0x0C48) return true;
    if (cp >= 0x0C4A && cp <= 0x0C4D) return true;
    if (cp >= 0x0C55 && cp <= 0x0C56) return true;
    if (cp >= 0x0C62 && cp <= 0x0C63) return true;
    if (cp >= 0x0C81 && cp <= 0x0C83) return true;
    if (cp == 0x0CBC) return true;
    if (cp >= 0x0CBE && cp <= 0x0CC4) return true;
    if (cp >= 0x0CC6 && cp <= 0x0CC8) return true;
    if (cp >= 0x0CCA && cp <= 0x0CCD) return true;
    if (cp >= 0x0CD5 && cp <= 0x0CD6) return true;
    if (cp >= 0x0CE2 && cp <= 0x0CE3) return true;
    if (cp >= 0x0D00 && cp <= 0x0D03) return true;
    if (cp >= 0x0D3B && cp <= 0x0D3C) return true;
    if (cp >= 0x0D3E && cp <= 0x0D44) return true;
    if (cp >= 0x0D46 && cp <= 0x0D48) return true;
    if (cp >= 0x0D4A && cp <= 0x0D4D) return true;
    if (cp == 0x0D57) return true;
    if (cp >= 0x0D62 && cp <= 0x0D63) return true;
    // Sinhala
    if (cp >= 0x0D82 && cp <= 0x0D83) return true;
    if (cp == 0x0DCA) return true;
    if (cp >= 0x0DCF && cp <= 0x0DD4) return true;
    if (cp >= 0x0DD6 && cp <= 0x0DD6) return true;
    if (cp >= 0x0DD8 && cp <= 0x0DDF) return true;

    // ── Thai + Lao combining marks ──────────────────────────────
    if (cp >= 0x0E31 && cp <= 0x0E31) return true;
    if (cp >= 0x0E34 && cp <= 0x0E3A) return true;
    if (cp >= 0x0E47 && cp <= 0x0E4E) return true;
    if (cp >= 0x0EB1 && cp <= 0x0EB1) return true;
    if (cp >= 0x0EB4 && cp <= 0x0EBC) return true;
    if (cp >= 0x0EC8 && cp <= 0x0ECD) return true;

    // ── Tibetan combining marks ─────────────────────────────────
    if (cp >= 0x0F18 && cp <= 0x0F19) return true;
    if (cp == 0x0F35) return true;
    if (cp == 0x0F37) return true;
    if (cp == 0x0F39) return true;
    if (cp >= 0x0F3E && cp <= 0x0F3F) return true;
    if (cp >= 0x0F71 && cp <= 0x0F84) return true;
    if (cp >= 0x0F86 && cp <= 0x0F87) return true;
    if (cp >= 0x0F8D && cp <= 0x0F97) return true;
    if (cp >= 0x0F99 && cp <= 0x0FBC) return true;
    if (cp == 0x0FC6) return true;

    // ── Myanmar combining marks ─────────────────────────────────
    if (cp >= 0x102B && cp <= 0x103E) return true;
    if (cp >= 0x1056 && cp <= 0x1059) return true;
    if (cp >= 0x105E && cp <= 0x1060) return true;
    if (cp >= 0x1062 && cp <= 0x1064) return true;
    if (cp >= 0x1067 && cp <= 0x106D) return true;
    if (cp >= 0x1071 && cp <= 0x1074) return true;
    if (cp >= 0x1082 && cp <= 0x108D) return true;
    if (cp == 0x108F) return true;
    if (cp >= 0x109A && cp <= 0x109D) return true;

    // ── Ethiopic combining marks ────────────────────────────────
    if (cp >= 0x135D && cp <= 0x135F) return true;

    // ── Mongolian combining marks ───────────────────────────────
    if (cp >= 0x1712 && cp <= 0x1714) return true;
    if (cp >= 0x1732 && cp <= 0x1734) return true;
    if (cp >= 0x1752 && cp <= 0x1753) return true;
    if (cp >= 0x1772 && cp <= 0x1773) return true;

    // ── Tagalog, Hanunoo, Buhid, Tagbanwa + Khmer combining ───
    if (cp >= 0x17B4 && cp <= 0x17D3) return true; // Khmer
    if (cp == 0x17DD) return true;

    // ── Canadian Syllabics combining ────────────────────────────
    if (cp >= 0x18A9 && cp <= 0x18A9) return true;

    // ── Limbu combining marks ───────────────────────────────────
    if (cp >= 0x1920 && cp <= 0x192B) return true;
    if (cp >= 0x1930 && cp <= 0x193B) return true;

    // ── Tai Le, New Tai Lue combining marks ─────────────────────
    if (cp == 0x1A17 || cp == 0x1A18) return true;
    if (cp >= 0x1A55 && cp <= 0x1A5E) return true;
    if (cp >= 0x1A60 && cp <= 0x1A7C) return true;
    if (cp == 0x1A7F) return true;
    if (cp >= 0x1AB0 && cp <= 0x1ACE) return true;
    if (cp >= 0x1B00 && cp <= 0x1B04) return true;
    if (cp >= 0x1B34 && cp <= 0x1B44) return true;
    if (cp >= 0x1B6B && cp <= 0x1B73) return true;
    if (cp >= 0x1B80 && cp <= 0x1B82) return true;
    if (cp >= 0x1BA1 && cp <= 0x1BAD) return true;
    if (cp >= 0x1BE6 && cp <= 0x1BF3) return true;

    // ── CJK-related combining / enclosing ───────────────────────
    if (cp >= 0x302A && cp <= 0x302F) return true;
    if (cp == 0x3099 || cp == 0x309A) return true;

    // ── Enclosing combining marks ───────────────────────────────
    if (cp >= 0x20E0 && cp <= 0x20E3) return true;
    if (cp == 0x20E4) return true;

    // ── Wide coverage: Mn/Mc/Me General_Category catch-all ──────
    if (cp >= 0xA8E0 && cp <= 0xA8FF) return true;
    if (cp >= 0xA926 && cp <= 0xA92D) return true;
    if (cp >= 0xA947 && cp <= 0xA953) return true;
    if (cp >= 0xA980 && cp <= 0xA983) return true;
    if (cp >= 0xA9B3 && cp <= 0xA9C0) return true;
    if (cp >= 0xAA29 && cp <= 0xAA36) return true;
    if (cp >= 0xAA43 && cp <= 0xAA43) return true;
    if (cp >= 0xAA4C && cp <= 0xAA4D) return true;
    if (cp >= 0xAA7B && cp <= 0xAA7D) return true;
    if (cp >= 0xAAB0 && cp <= 0xAAB0) return true;
    if (cp >= 0xAAB2 && cp <= 0xAAB4) return true;
    if (cp >= 0xAAB7 && cp <= 0xAAB8) return true;
    if (cp >= 0xAABE && cp <= 0xAABF) return true;
    if (cp == 0xAAC1) return true;
    if (cp >= 0xABE3 && cp <= 0xABEA) return true;
    if (cp >= 0xABEC && cp <= 0xABED) return true;

    return false;
}

/// True for Regional Indicator code points (U+1F1E6–U+1F1FF).
/// Two consecutive RIs form a single grapheme cluster (flag emoji).
inline bool is_regional_indicator(char32_t cp) noexcept {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

/// True if `cp` has the Unicode Extended_Pictographic property.
/// Approximation covering the most common emoji and pictographic ranges
/// (SMP emoji blocks, Misc Symbols, Dingbats, and common BMP pictographs).
/// Used by GB11 (ZWJ emoji sequence) grapheme segmentation.
inline bool is_extended_pictographic(char32_t cp) noexcept {
    // ── SMP emoji / pictographic blocks ───────────────────────────
    // Supplemental Symbols and Pictographs, Emoticons, Transport,
    // and other pictorial SMP ranges.
    if (cp >= 0x1F000 && cp <= 0x1F9FF) return true;
    if (cp >= 0x1FA00 && cp <= 0x1FAFF) return true;

    // ── Misc Symbols and Pictographs (BMP) ────────────────────────
    if (cp >= 0x2600 && cp <= 0x27BF) return true;

    // ── Dingbats (BMP) ────────────────────────────────────────────
    if (cp >= 0x2700 && cp <= 0x27BF) return true;  // overlap, kept for clarity

    // ── Ornamental and other pictorial BMP ────────────────────────
    if (cp >= 0x1F300 && cp <= 0x1F5FF) return true;  // already in 1F000-1F9FF

    // ── Common BMP pictographs used as emoji ──────────────────────
    if (cp == 0x00A9 || cp == 0x00AE) return true;  // © ®
    if (cp == 0x203C || cp == 0x2049) return true;  // ‼ ⁉
    if (cp == 0x2122 || cp == 0x2139) return true;  // ™ ℹ
    if (cp >= 0x2194 && cp <= 0x2199) return true;  // ↔ ↕ ↖ ↗ ↘ ↙
    if (cp >= 0x21A9 && cp <= 0x21AA) return true;  // ↩ ↪
    if (cp >= 0x231A && cp <= 0x231B) return true;  // ⌚ ⌛
    if (cp == 0x2328 || cp == 0x23CF) return true;  // ⌨ ⏏
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;  // ⏩–⏳
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;  // ⏸–⏺
    if (cp == 0x24C2) return true;                   // Ⓜ
    if (cp >= 0x25AA && cp <= 0x25AB) return true;  // ▪ ▫
    if (cp == 0x25B6 || cp == 0x25C0) return true;  // ▶ ◀
    if (cp >= 0x25FB && cp <= 0x25FE) return true;  // ◻–◾
    if (cp >= 0x2600 && cp <= 0x27BF) return true;  // ☀–➿ (already covered)
    if (cp >= 0x2934 && cp <= 0x2935) return true;  // ⤴ ⤵
    if (cp >= 0x2B05 && cp <= 0x2B07) return true;  // ⬅ ⬆ ⬇
    if (cp >= 0x2B1B && cp <= 0x2B1C) return true;  // ⬛ ⬜
    if (cp == 0x2B50 || cp == 0x2B55) return true;  // ⭐ ⭕
    if (cp == 0x3030 || cp == 0x303D) return true;  // 〰 〽
    if (cp == 0x3297 || cp == 0x3299) return true;  // ㊗ ㊙

    return false;
}

/// Count the number of extended grapheme clusters in a UTF-8 string.
/// Combining marks, ZWJ sequences, emoji modifiers, and regional-indicator
/// flag pairs are not counted as separate clusters — they stay attached
/// to their base character.
///
/// Implements UAX #29 GB11: Extended_Pictographic Extend* ZWJ ×
/// Extended_Pictographic.  An emoji ZWJ sequence (e.g. family emoji) is
/// counted as a single grapheme cluster.
inline size_t grapheme_cluster_count(std::string_view sv) {
    size_t count = 0;
    size_t ri_toggle = 0;  // 0 = expecting first RI, 1 = expecting second

    // GB11 state machine for emoji ZWJ sequences:
    //   Normal  → ExtPict when we see an Extended_Pictographic
    //   ExtPict → ExtPictZWJ when we see ZWJ (U+200D) after ExtPict + optional Extend*
    //   ExtPictZWJ → stays ExtPictZWJ on more Extend, returns to Normal on
    //                 non-ExtPict, non-Extend (breaking the ZWJ chain)
    // When state == ExtPictZWJ and we see Extended_Pictographic, it continues the
    // current cluster (GB11: ExtPict Extend* ZWJ × ExtPict).
    enum class GB11State { Normal, ExtPict, ExtPictZWJ };
    GB11State gb11 = GB11State::Normal;

    for (size_t i = 0; i < sv.size();) {
        const char32_t cp = utf8_decode_cp(sv, i);

        const bool is_ext  = is_grapheme_extend(cp);
        const bool is_ri   = is_regional_indicator(cp);
        const bool is_ep   = is_extended_pictographic(cp);
        const bool is_zwj  = (cp == 0x200D);

        if (is_ri) {
            if (ri_toggle == 0) ++count;  // first RI of a flag
            ri_toggle ^= 1;               // toggle 0↔1
            gb11 = GB11State::Normal;     // RI breaks any ZWJ chain
            continue;
        }

        // GB11: ZWJ after Extended_Pictographic transitions to ExtPictZWJ.
        // Subsequent Extend characters keep the current state.
        if (is_zwj && gb11 == GB11State::ExtPict) {
            gb11 = GB11State::ExtPictZWJ;
            ri_toggle = 0;
            continue;
        }

        if (is_ext) {
            // Extend keeps the current GB11 state (doesn't break ZWJ chain).
            ri_toggle = 0;
            continue;
        }

        // GB11: Extended_Pictographic after ExtPictZWJ continues the
        // current cluster (the ZWJ glues emoji together).
        if (is_ep && gb11 == GB11State::ExtPictZWJ) {
            gb11 = GB11State::ExtPict;  // back to ExtPict (ZWJ consumed)
            ri_toggle = 0;
            continue;  // no new cluster
        }

        // Non-extender, non-RI, non-ZWJ-sequenced picto: starts a new cluster.
        ++count;
        ri_toggle = 0;
        gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
    }

    return count;
}

/// Decode a UTF-8 code point at the given byte offset without advancing.
/// Returns the code point, or U+FFFD if the sequence is invalid or
/// would run past the end of the string_view.
/// Used by grapheme_byte_offset_at for peek operations at multiple
/// call sites (main loop, RI pair check, trailing extender loop).
inline char32_t utf8_decode_at(std::string_view sv, size_t offset) {
    if (offset >= sv.size()) return 0xFFFD;
    const unsigned char lead = static_cast<unsigned char>(sv[offset]);
    const size_t len = utf8_seq_len(lead);
    if (offset + len > sv.size()) return 0xFFFD;
    switch (len) {
        case 1: return lead;
        case 2: return ((lead & 0x1F) << 6) | (static_cast<unsigned char>(sv[offset + 1]) & 0x3F);
        case 3: return ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(sv[offset + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset + 2]) & 0x3F);
        case 4: return ((lead & 0x07) << 18) | ((static_cast<unsigned char>(sv[offset + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(sv[offset + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset + 3]) & 0x3F);
        default: return 0xFFFD;
    }
}

/// Find the byte offset after exactly N complete grapheme clusters.
/// Returns sv.size() if N exceeds the string length.  Safe for
/// std::string::substr() — never splits a grapheme cluster.
/// Implements UAX #29 GB11 for emoji ZWJ sequences and GB12/GB13 for
/// Regional Indicator flag pairs.
inline size_t grapheme_byte_offset_at(std::string_view sv, size_t n) {
    if (n == 0) return 0;

    size_t clusters = 0;
    size_t offset = 0;
    size_t ri_toggle = 0;

    // GB11 state machine (same as grapheme_cluster_count above).
    enum class GB11State { Normal, ExtPict, ExtPictZWJ };
    GB11State gb11 = GB11State::Normal;

    while (offset < sv.size() && clusters < n) {
        const unsigned char lead = static_cast<unsigned char>(sv[offset]);
        const size_t len = utf8_seq_len(lead);
        if (offset + len > sv.size()) break;

        const char32_t cp = utf8_decode_at(sv, offset);

        const bool is_ext = is_grapheme_extend(cp);
        const bool is_ri  = is_regional_indicator(cp);
        const bool is_ep  = is_extended_pictographic(cp);
        const bool is_zwj = (cp == 0x200D);

        if (is_ri) {
            if (ri_toggle == 0) {
                ++clusters;
                if (clusters == n) {
                    // Advance past the full RI pair, but only if next is RI.
                    offset += len;
                    if (offset < sv.size()) {
                        const unsigned char lead2 = static_cast<unsigned char>(sv[offset]);
                        const size_t len2 = utf8_seq_len(lead2);
                        if (offset + len2 <= sv.size()) {
                        const char32_t next_cp = utf8_decode_at(sv, offset);
                        if (is_regional_indicator(next_cp)) {
                                offset += len2;
                            }
                        }
                    }
                    return offset;
                }
            }
            ri_toggle ^= 1;
            offset += len;
            gb11 = GB11State::Normal;
            continue;
        }

        // GB11: ZWJ after Extended_Pictographic → ExtPictZWJ state.
        if (is_zwj && gb11 == GB11State::ExtPict) {
            gb11 = GB11State::ExtPictZWJ;
            ri_toggle = 0;
            offset += len;
            continue;
        }

        if (is_ext) {
            ri_toggle = 0;
            offset += len;
            continue;
        }

        // GB11: Extended_Pictographic after ExtPictZWJ continues current cluster.
        if (is_ep && gb11 == GB11State::ExtPictZWJ) {
            gb11 = GB11State::ExtPict;
            ri_toggle = 0;
            offset += len;
            continue;  // no new cluster
        }

        // Non-extender, non-RI, non-ZWJ-sequenced: starts a new cluster.
        ++clusters;
        ri_toggle = 0;
        gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
        if (clusters == n) {
            offset += len;
            // Consume trailing extenders AND GB11 ZWJ sequences that
            // belong to this cluster.  Mirrors the GB11 state machine
            // from grapheme_cluster_count: ExtPict → ZWJ → ExtPictZWJ,
            // and ExtPictZWJ + ExtPict continues the same cluster.
            while (offset < sv.size()) {
                const unsigned char next_lead = static_cast<unsigned char>(sv[offset]);
                const size_t next_len = utf8_seq_len(next_lead);
                if (offset + next_len > sv.size()) break;
                const char32_t next_cp = utf8_decode_at(sv, offset);
                const bool next_ext = is_grapheme_extend(next_cp);
                const bool next_zwj = (next_cp == 0x200D);
                const bool next_ep  = is_extended_pictographic(next_cp);

                // GB11: Extended_Pictographic after ExtPictZWJ continues the cluster.
                if (next_ep && gb11 == GB11State::ExtPictZWJ) {
                    gb11 = GB11State::ExtPict;
                    offset += next_len;
                    continue;
                }
                // ZWJ after ExtPict → transition to ExtPictZWJ.
                if (next_zwj && gb11 == GB11State::ExtPict) {
                    gb11 = GB11State::ExtPictZWJ;
                    offset += next_len;
                    continue;
                }
                // Any Extend keeps the current GB11 state.
                if (next_ext) {
                    offset += next_len;
                    continue;
                }
                break;  // non-extender, non-GB11: end of cluster
            }
            return offset;
        }
        offset += len;
    }

    return sv.size();
}

} // namespace detail

struct TextLayoutRun {
    std::string text;
    TextStyle style{};
    bool is_line_break{false};
    bool is_space{false};
    bool is_decorative_star{false};
    bool use_advance_override{false};
    float advance_override{0.0f};
    float star_inner_radius{0.0f};
    float star_outer_radius{0.0f};
    int star_points{8};
};

struct TextLayoutLineRun {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
    TextStyle style{};
    bool is_space{false};
    bool is_decorative_star{false};
    float star_inner_radius{0.0f};
    float star_outer_radius{0.0f};
    int star_points{8};
};

struct TextLayoutInput {
    std::string text;
    std::vector<TextLayoutRun> runs;
    TextStyle style{};
    TextBox box{};
    // Legacy per-char callback (used when font_engine is null)
    const void* char_width_ctx{nullptr};
    float (*char_width_fn)(const void* ctx, char c, float font_size){nullptr};
    // FreeType/HarfBuzz-based measurement (used by TextAnimator for per-glyph bbox)
    const FontEngine* font_engine{nullptr};
    FontSpec font_spec{};
    // Blend2D-based measurement (preferred for layout; eliminates FT+HB vs B2D
    // discrepancy since the same engine measures and renders).  bl_font_ptr
    // points to a BLFontFace; bl_measure_fn creates BLFont at requested size.
    const void* bl_font_ptr{nullptr};
    float (*bl_measure_fn)(const void* font_face, std::string_view text, float font_size){nullptr};
};

struct TextLayoutLine {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
    float baseline{0.0f};
    std::vector<TextLayoutLineRun> runs;
};

struct TextLayoutResult {
    Vec2 size{0.0f, 0.0f};
    std::vector<TextLayoutLine> lines;
    float font_size{0.0f};
};

class TextLayoutEngine {
private:
    static float measure_char_legacy(const TextLayoutInput& input, char c, float font_size) {
        if (input.char_width_fn) {
            return std::max(0.0f, input.char_width_fn(input.char_width_ctx, c, font_size));
        }
        return font_size * 0.6f;
    }

    static float measure_string_legacy(const TextLayoutInput& input, std::string_view s, float font_size) {
        float width = 0.0f;
        const size_t cp_count = detail::utf8_length(s);
        // Legacy path uses per-char callback; iterate byte-by-byte for ASCII
        // since legacy callbacks only support single-byte chars anyway
        for (char c : s) {
            width += measure_char_legacy(input, c, font_size);
        }
        const size_t n_clusters2 = detail::grapheme_cluster_count(s);
        width += input.style.tracking * static_cast<float>(n_clusters2 > 0 ? n_clusters2 - 1 : 0);
        return std::max(0.0f, width);
    }

    // Fast single-character width lookup using precomputed ASCII table.
    // Prefers Blend2D when available (same engine as rasterization).
    // Falls back to font_engine measurement for non-ASCII or font mismatch.
    static float measure_single_char(char c, bool have_precomputed,
                                      const std::array<float, 256>& char_widths,
                                      const TextLayoutInput& input,
                                      const TextStyle& style, float font_size) {
        // Precomputed table check (works for both B2D and FT+HB paths)
        if (have_precomputed && style.font_path.empty() && style.size <= 0.0f) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (char_widths[uc] > 0.0f) return char_widths[uc];
        }
        // Blend2D fast path for single character
        if (input.bl_measure_fn && input.bl_font_ptr && style.font_path.empty()) {
            char buf[2] = {c, '\0'};
            return input.bl_measure_fn(input.bl_font_ptr, std::string_view(buf, 1), font_size);
        }
        if (input.font_engine) {
            const FontSpec spec = FontSpec{
                .font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path,
                .font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family,
                .font_weight = style.font_weight ? style.font_weight : input.font_spec.font_weight,
                .font_style = style.font_style.empty() ? input.font_spec.font_style : style.font_style,
            };
            char buf[2] = {c, '\0'};
            return input.font_engine->measure_text(buf, spec, font_size, style.shaping);
        }
        return measure_char_legacy(input, c, font_size);
    }

    static float measure_string(const TextLayoutInput& input, const TextStyle& style, std::string_view s, float font_size) {
        // Blend2D path (preferred): uses same engine as rasterization,
        // eliminating the FreeType+HarfBuzz vs Blend2D width discrepancy.
        // Only used when the style inherits the input font (no per-run override).
        if (input.bl_measure_fn && input.bl_font_ptr && style.font_path.empty()) {
            float width = input.bl_measure_fn(input.bl_font_ptr, s, font_size);
            const size_t n_clusters = detail::grapheme_cluster_count(s);
        width += style.tracking * static_cast<float>(n_clusters > 0 ? n_clusters - 1 : 0);
            return std::max(0.0f, width);
        }
        if (input.font_engine) {
            const FontSpec spec = FontSpec{
                .font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path,
                .font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family,
                .font_weight = style.font_weight ? style.font_weight : input.font_spec.font_weight,
                .font_style = style.font_style.empty() ? input.font_spec.font_style : style.font_style,
            };
            float width = input.font_engine->measure_text(s, spec, font_size, style.shaping);
            const size_t n_clusters = detail::grapheme_cluster_count(s);
        width += style.tracking * static_cast<float>(n_clusters > 0 ? n_clusters - 1 : 0);
            return std::max(0.0f, width);
        }
        return measure_string_legacy(input, s, font_size);
    }

    static float measure_run_width(const TextLayoutInput& input, const TextLayoutRun& run, float font_size) {
        if (run.use_advance_override) {
            return std::max(0.0f, run.advance_override);
        }
        return measure_string(input, run.style, run.text, font_size);
    }

    static FontSpec resolve_font_spec(const TextLayoutInput& input, const TextStyle& style) {
        FontSpec spec;
        spec.font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path;
        spec.font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family;
        spec.font_weight = style.font_weight;
        spec.font_style = style.font_style;
        return spec;
    }

    static TextLayoutLineRun make_run(
        const TextLayoutInput& input,
        const TextLayoutRun& run,
        float x,
        float y
    ) {
        TextLayoutLineRun out;
        out.text = run.text;
        out.position = {x, y};
        out.style = run.style;
        out.is_space = run.is_space;
        out.is_decorative_star = run.is_decorative_star;
        out.star_inner_radius = run.star_inner_radius;
        out.star_outer_radius = run.star_outer_radius;
        out.star_points = run.star_points;
        out.width = measure_run_width(input, run, std::max(1.0f, run.style.size));
        return out;
    }

    static TextLayoutResult layout_single_run(const TextLayoutInput& input) {
        TextLayoutResult result;
        const float font_size = std::max(1.0f, input.style.size);
        result.font_size = font_size;
        const float line_height = std::max(1.0f, font_size * input.style.line_height);
        const float max_width = input.box.enabled && input.box.size.x > 0.0f ? input.box.size.x : 0.0f;

        // Precompute ASCII character widths at this font size for O(1)
        // lookup during wrapping.  Uses Blend2D when available (same engine
        // as rasterization), falling back to FT+HarfBuzz otherwise.
        std::array<float, 256> char_widths{};
        const bool use_bl2d = input.bl_measure_fn != nullptr && input.bl_font_ptr != nullptr;
        const bool have_precomputed = use_bl2d || input.font_engine != nullptr;
        if (have_precomputed) {
            for (int c = 32; c < 127; ++c) {
                char buf[2] = {static_cast<char>(c), '\0'};
                if (use_bl2d) {
                    char_widths[c] = input.bl_measure_fn(
                        input.bl_font_ptr, std::string_view(buf, 1), font_size);
                } else {
                    char_widths[c] = input.font_engine->measure_text(
                        buf, input.font_spec, font_size, input.style.shaping);
                }
            }
        }

        auto measure_char = [&](char c) -> float {
            if (have_precomputed) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (char_widths[uc] > 0.0f) return char_widths[uc];
            }
            if (use_bl2d) {
                char buf[2] = {c, '\0'};
                return input.bl_measure_fn(
                    input.bl_font_ptr, std::string_view(buf, 1), font_size);
            }
            if (input.font_engine) {
                char buf[2] = {c, '\0'};
                return input.font_engine->measure_text(buf, input.font_spec, font_size, input.style.shaping);
            }
            return measure_char_legacy(input, c, font_size);
        };

        auto measure_string_input = [&](std::string_view s) -> float {
            return measure_string(input, input.style, s, font_size);
        };

        std::vector<std::string> raw_lines;
        std::string current_line;
        float current_width = 0.0f;

        auto push_current_line = [&]() {
            raw_lines.push_back(std::move(current_line));
            current_line.clear();
            current_width = 0.0f;
        };

        std::vector<std::pair<std::string, bool>> tokens;
        std::string current_token;
        bool in_space = false;

        // UTF-8 aware tokenization: iterate by code-point, not by byte
        for (size_t i = 0; i < input.text.size();) {
            size_t len = detail::utf8_seq_len(static_cast<unsigned char>(input.text[i]));
            if (len == 1) {
                const char c = input.text[i];
                if (c == '\n') {
                    if (!current_token.empty()) {
                        tokens.push_back({current_token, in_space});
                        current_token.clear();
                    }
                    tokens.push_back({"\n", false});
                    i += 1;
                    continue;
                } else if (c == ' ' || c == '\t') {
                    if (!current_token.empty() && !in_space) {
                        tokens.push_back({current_token, false});
                        current_token.clear();
                    }
                    in_space = true;
                    current_token.push_back(c);
                    i += 1;
                    continue;
                }
            }
            // Non-space, non-newline (multi-byte or regular char)
            if (!current_token.empty() && in_space) {
                tokens.push_back({current_token, true});
                current_token.clear();
            }
            in_space = false;
            current_token.append(input.text, i, len);
            i += len;
        }
        if (!current_token.empty()) {
            tokens.push_back({current_token, in_space});
        }

        // ── Record line widths during push, not via post-hoc re-measurement ──
        std::vector<float> line_widths;
        auto push_line_with_width = [&]() {
            raw_lines.push_back(std::move(current_line));
            line_widths.push_back(current_width);
            current_line.clear();
            current_width = 0.0f;
        };

        const bool wrapping_enabled = max_width > 0.0f && input.style.wrap != TextWrap::None;

        for (const auto& token_pair : tokens) {
            const std::string& token = token_pair.first;
            const bool is_space = token_pair.second;

            if (token == "\n") {
                push_line_with_width();
                continue;
            }

            if (!wrapping_enabled) {
                current_line += token;
                current_width += measure_string_input(token);
                continue;
            }

            const float token_w = measure_string_input(token);

            if (input.style.wrap == TextWrap::Word) {
                if (current_width + token_w > max_width) {
                    if (!current_line.empty()) {
                        push_line_with_width();
                        if (is_space) continue;
                    }
                    if (token_w > max_width) {
                    // Grapheme-cluster-aware character fallback for overlong tokens.
                    // Never splits combining marks, ZWJ sequences, or flag pairs.
                    const size_t total_clusters = detail::grapheme_cluster_count(token);
                    const float avg_cluster_w = total_clusters > 0
                        ? token_w / static_cast<float>(total_clusters) : token_w;
                    for (size_t ci = 0; ci < token.size();) {
                        std::string_view suffix(token.data() + ci, token.size() - ci);
                        const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                        const float cw = avg_cluster_w;
                        if (current_width + cw > max_width && !current_line.empty()) {
                            push_line_with_width();
                        }
                        current_line.append(token.data() + ci, cluster_len);
                        current_width += cw;
                        ci += cluster_len;
                    }
                    } else {
                        current_line = token;
                        current_width = token_w;
                    }
                } else {
                    current_line += token;
                    current_width += token_w;
                }
            } else if (input.style.wrap == TextWrap::Character) {
                // Grapheme-cluster-aware character wrap.
                // Never splits combining marks, ZWJ sequences, or flag pairs.
                const size_t total_clusters = detail::grapheme_cluster_count(token);
                const float avg_cluster_w = total_clusters > 0
                    ? token_w / static_cast<float>(total_clusters) : token_w;
                for (size_t ci = 0; ci < token.size();) {
                    std::string_view suffix(token.data() + ci, token.size() - ci);
                    const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                    const float cw = avg_cluster_w;
                    if (current_width + cw > max_width && !current_line.empty()) {
                        push_line_with_width();
                    }
                    current_line.append(token.data() + ci, cluster_len);
                    current_width += cw;
                    ci += cluster_len;
                }
            }
        }

        if (!current_line.empty() || raw_lines.empty()) {
            raw_lines.push_back(std::move(current_line));
            line_widths.push_back(current_width);
        }

        // Re-use raw_lines as the tokenizer variable is no longer needed
        tokens.clear();
        tokens.shrink_to_fit();

        const int max_allowed_lines = input.style.max_lines;
        const bool apply_ellipsis = input.style.ellipsis || input.style.overflow == TextOverflow::Ellipsis;

        if (max_allowed_lines > 0 && static_cast<int>(raw_lines.size()) > max_allowed_lines) {
            raw_lines.resize(max_allowed_lines);
            line_widths.resize(max_allowed_lines);
            if (apply_ellipsis && !raw_lines.empty()) {
                std::string& last_line = raw_lines.back();
                float& last_line_w = line_widths.back();
                const float ellipsis_w = measure_string_input("...");
                // Grapheme-cluster-aware trim: remove whole clusters, never
                // splitting combining marks, ZWJ sequences, or flag pairs.
                while (!last_line.empty() && last_line_w + ellipsis_w > max_width) {
                    const size_t num_clusters = detail::grapheme_cluster_count(last_line);
                    if (num_clusters <= 1) {
                        last_line.clear();
                        last_line_w = 0.0f;
                        break;
                    }
                    const size_t trim_to = detail::grapheme_byte_offset_at(
                        last_line, num_clusters - 1);
                    const std::string_view removed_suffix(
                        last_line.data() + trim_to, last_line.size() - trim_to);
                    const float removed_w = measure_string_input(removed_suffix);
                    last_line.resize(trim_to);
                    last_line_w -= removed_w;
                }
                last_line += "...";
                last_line_w += ellipsis_w;
            }
        }

        float max_seen_width = 0.0f;
        for (const auto w : line_widths) {
            max_seen_width = std::max(max_seen_width, w);
        }

        result.size.x = max_seen_width;
        result.size.y = static_cast<float>(raw_lines.size()) * line_height;

        for (size_t i = 0; i < raw_lines.size(); ++i) {
            TextLayoutLine line;
            line.text = raw_lines[i];
            line.width = line_widths[i];
            line.ascent = font_size * 0.78f;
            line.descent = font_size * 0.22f;
            line.baseline = line.ascent;

            float dx = 0.0f;
            if (input.style.align == TextAlign::Center) {
                dx = (max_seen_width - line.width) * 0.5f;
            } else if (input.style.align == TextAlign::Right) {
                dx = max_seen_width - line.width;
            }
            line.position = {dx, static_cast<float>(i) * line_height};

            TextLayoutLineRun run;
            run.text = line.text;
            run.position = {line.position.x, 0.0f};
            run.width = line.width;
            run.style = input.style;
            line.runs.push_back(std::move(run));

            result.lines.push_back(std::move(line));
        }

        return result;
    }

    static TextLayoutResult layout_inline_runs(const TextLayoutInput& input) {
        TextLayoutResult result;
        const float font_size_hint = std::max(1.0f, input.style.size);
        result.font_size = font_size_hint;

        // Precompute ASCII character widths (same approach as layout_single_run).
        // Uses Blend2D when available, falling back to FT+HarfBuzz.
        std::array<float, 256> char_widths{};
        const bool use_bl2d = input.bl_measure_fn != nullptr && input.bl_font_ptr != nullptr;
        const bool have_precomputed = use_bl2d || input.font_engine != nullptr;
        if (have_precomputed) {
            for (int c = 32; c < 127; ++c) {
                char buf[2] = {static_cast<char>(c), '\0'};
                if (use_bl2d) {
                    char_widths[c] = input.bl_measure_fn(
                        input.bl_font_ptr, std::string_view(buf, 1), font_size_hint);
                } else {
                    char_widths[c] = input.font_engine->measure_text(
                        buf, input.font_spec, font_size_hint, input.style.shaping);
                }
            }
        }

        struct LineState {
            std::vector<TextLayoutLineRun> runs;
            float width{0.0f};
            float ascent{0.0f};
            float descent{0.0f};
        };

        std::vector<LineState> lines;
        LineState current;
        const float max_width_limit = input.box.enabled && input.box.size.x > 0.0f ? input.box.size.x : 0.0f;
        const bool wrapping_enabled = max_width_limit > 0.0f && input.style.wrap != TextWrap::None;
        const bool word_wrap = wrapping_enabled && input.style.wrap == TextWrap::Word;
        const bool char_wrap = wrapping_enabled && input.style.wrap == TextWrap::Character;

        auto push_current = [&]() {
            lines.push_back(std::move(current));
            current = LineState{};
        };

        auto append_piece = [&](const TextLayoutRun& source, std::string text_piece) {
            if (text_piece.empty() && !source.is_space && !source.is_decorative_star && !source.use_advance_override) {
                return;
            }

            TextLayoutRun piece = source;
            piece.text = std::move(text_piece);
            if (piece.is_decorative_star) {
                piece.text.clear();
            }
            if (!piece.style.font_path.empty()) {
                // keep explicit style
            } else {
                piece.style.font_path = input.style.font_path;
            }
            if (piece.style.font_family.empty()) {
                piece.style.font_family = input.style.font_family;
            }
            if (piece.style.font_weight == 0) {
                piece.style.font_weight = input.style.font_weight;
            }
            if (piece.style.font_style.empty()) {
                piece.style.font_style = input.style.font_style;
            }
            if (piece.style.size <= 0.0f) {
                piece.style.size = input.style.size;
            }
            if (piece.style.line_height <= 0.0f) {
                piece.style.line_height = input.style.line_height;
            }

            const float run_size = std::max(1.0f, piece.style.size);
            const float ascent = run_size * 0.78f;
            const float descent = run_size * 0.22f;
            const float width = measure_run_width(input, piece, run_size);

            TextLayoutLineRun layout_run;
            layout_run.text = piece.text;
            layout_run.style = piece.style;
            layout_run.width = width;
            layout_run.is_space = piece.is_space;
            layout_run.is_decorative_star = piece.is_decorative_star;
            layout_run.star_inner_radius = piece.star_inner_radius;
            layout_run.star_outer_radius = piece.star_outer_radius;
            layout_run.star_points = piece.star_points;
            layout_run.position = {current.width, 0.0f};

            current.width += width;
            current.ascent = std::max(current.ascent, ascent);
            current.descent = std::max(current.descent, descent);
            current.runs.push_back(std::move(layout_run));
        };

        auto append_split_text = [&](const TextLayoutRun& run, std::string_view text) {
            if (text.empty()) {
                if (run.is_space || run.is_decorative_star || run.use_advance_override) {
                    append_piece(run, "");
                }
                return;
            }

            const float run_size = std::max(1.0f, run.style.size <= 0.0f ? input.style.size : run.style.size);

            if (!wrapping_enabled) {
                append_piece(run, std::string(text));
                return;
            }

            if (char_wrap) {
                // Grapheme-cluster-aware character wrap.
                // Never splits combining marks, ZWJ sequences, or flag pairs.
                const float full_width = measure_string(input, run.style, text, run_size);
                const size_t total_clusters = detail::grapheme_cluster_count(text);
                const float avg_cluster_w = total_clusters > 0
                    ? full_width / static_cast<float>(total_clusters) : run_size * 0.6f;
                for (size_t ci = 0; ci < text.size();) {
                    std::string_view suffix(text.data() + ci, text.size() - ci);
                    const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                    // Handle \r and \n within the cluster (single-byte guard)
                    if (cluster_len == 1) {
                        if (text[ci] == '\r') { ci += 1; continue; }
                        if (text[ci] == '\n') {
                            if (!current.runs.empty()) {
                                push_current();
                            }
                            ci += 1;
                            continue;
                        }
                    }

                    const float cw = avg_cluster_w;
                    if (current.width + cw > max_width_limit && !current.runs.empty()) {
                        push_current();
                    }
                    if (cluster_len == 1 && (text[ci] == ' ' || text[ci] == '\t')) {
                        if (current.runs.empty()) {
                            ci += 1;
                            continue;
                        }
                    }
                    std::string glyph(text, ci, cluster_len);
                    append_piece(run, glyph);
                    ci += cluster_len;
                }
                return;
            }

            // Word wrap
            std::string token;
            bool token_is_space = false;

            auto flush_token = [&]() {
                if (token.empty()) {
                    return;
                }

                const float token_w = measure_string(input, run.style, token, run_size);

                if (token_is_space) {
                    if (current.runs.empty()) {
                        token.clear();
                        return;
                    }
                    if (current.width + token_w > max_width_limit && !current.runs.empty()) {
                        push_current();
                        token.clear();
                        return;
                    }
                    append_piece(run, token);
                    token.clear();
                    return;
                }

                if (current.width > 0.0f && current.width + token_w > max_width_limit) {
                    push_current();
                }

                if (token_w > max_width_limit && current.runs.empty()) {
                    // Grapheme-cluster-aware character fallback for overlong tokens.
                    const size_t total_clusters = detail::grapheme_cluster_count(token);
                    const float avg_cluster_w = total_clusters > 0
                        ? token_w / static_cast<float>(total_clusters) : token_w;
                    for (size_t ci = 0; ci < token.size();) {
                        std::string_view suffix(token.data() + ci, token.size() - ci);
                        const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                        const float cw = avg_cluster_w;
                        if (current.width + cw > max_width_limit && !current.runs.empty()) {
                            push_current();
                        }
                        const std::string glyph(token, ci, cluster_len);
                        append_piece(run, glyph);
                        ci += cluster_len;
                    }
                } else {
                    append_piece(run, token);
                }

                token.clear();
            };

            // UTF-8 aware word-wrap tokenization
            for (size_t ci = 0; ci < text.size();) {
                size_t clen = detail::utf8_seq_len(static_cast<unsigned char>(text[ci]));
                if (clen == 1 && text[ci] == '\r') {
                    ci += 1;
                    continue;
                }
                if (clen == 1 && text[ci] == '\n') {
                    flush_token();
                    if (!current.runs.empty()) {
                        push_current();
                    }
                    ci += 1;
                    continue;
                }

                const bool is_space = (clen == 1 && (text[ci] == ' ' || text[ci] == '\t'));
                if (token.empty()) {
                    token_is_space = is_space;
                    if (clen == 1) token.push_back(text[ci]);
                    else token.append(text, ci, clen);
                    ci += clen;
                    continue;
                }
                if (token_is_space != is_space) {
                    flush_token();
                    token_is_space = is_space;
                }
                if (clen == 1) token.push_back(text[ci]);
                else token.append(text, ci, clen);
                ci += clen;
            }
            flush_token();
        };

        auto split_and_add = [&](const TextLayoutRun& run) {
            if (run.is_line_break) {
                if (!current.runs.empty()) {
                    push_current();
                }
                return;
            }

            const std::string& text = run.text;
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == '\n') {
                    append_split_text(run, text.substr(start, i - start));
                    if (i < text.size() && text[i] == '\n') {
                        if (!current.runs.empty()) {
                            push_current();
                        }
                    }
                    start = i + 1;
                }
            }
        };

        if (input.runs.empty()) {
            TextLayoutRun single;
            single.text = input.text;
            single.style = input.style;
            split_and_add(single);
        } else {
            for (const auto& run : input.runs) {
                split_and_add(run);
            }
        }

        if (!current.runs.empty() || lines.empty()) {
            lines.push_back(std::move(current));
        }

        const float line_height = std::max(1.0f, input.style.size * input.style.line_height);
        float max_width = 0.0f;
        for (const auto& line : lines) {
            max_width = std::max(max_width, line.width);
        }

        const bool apply_ellipsis = input.style.ellipsis || input.style.overflow == TextOverflow::Ellipsis;

        if (apply_ellipsis) {
            for (auto& line : lines) {
                if (line.width <= max_width_limit || line.runs.empty()) continue;
                if (line.runs.size() == 1) {
                    auto& run = line.runs.front();
                    const float run_size = std::max(1.0f, run.style.size <= 0.0f ? font_size_hint : run.style.size);
                    // Grapheme-cluster-aware trim: remove whole clusters.
                    while (!run.text.empty() && line.width > max_width_limit) {
                        const size_t num_clusters = detail::grapheme_cluster_count(run.text);
                        if (num_clusters <= 1) {
                            line.width -= run.width;
                            run.width = 0.0f;
                            run.text.clear();
                            break;
                        }
                        const size_t trim_to = detail::grapheme_byte_offset_at(
                            run.text, num_clusters - 1);
                        run.text.resize(trim_to);
                        const float new_width = measure_string(input, run.style, run.text, run_size);
                        line.width -= (run.width - new_width);
                        run.width = new_width;
                    }
                    const float ellip_w = measure_string(input, run.style, "...", run_size);
                    run.text += "...";
                    run.width += ellip_w;
                    line.width += ellip_w;
                }
            }
        }

        result.size.x = max_width;
        result.size.y = static_cast<float>(lines.size()) * line_height;

        for (size_t i = 0; i < lines.size(); ++i) {
            auto& line_state = lines[i];
            TextLayoutLine line;
            line.width = line_state.width;
            line.ascent = std::max(line_state.ascent, font_size_hint * 0.78f);
            line.descent = std::max(line_state.descent, font_size_hint * 0.22f);
            line.baseline = line.ascent;
            line.position.y = static_cast<float>(i) * line_height;
            line.text.clear();
            for (const auto& run : line_state.runs) {
                line.text += run.text;
            }

            float dx = 0.0f;
            if (input.style.align == TextAlign::Center) {
                dx = (max_width - line.width) * 0.5f;
            } else if (input.style.align == TextAlign::Right) {
                dx = max_width - line.width;
            }
            line.position.x = dx;

            float cursor_x = dx;
            for (auto run : line_state.runs) {
                run.position.x = cursor_x;
                run.position.y = line.position.y;
                cursor_x += run.width;
                line.runs.push_back(std::move(run));
            }

            result.lines.push_back(std::move(line));
        }

        return result;
    }

public:
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& input) {
        TextLayoutInput current_input = input;
        const bool auto_fit = input.style.auto_fit || input.style.auto_scale;

        const auto measure_with_size = [&](float size) {
            TextLayoutInput test_input = current_input;
            test_input.style.size = size;
            return test_input.runs.empty() ? layout_single_run(test_input) : layout_inline_runs(test_input);
        };

        if (auto_fit && input.box.enabled && input.box.size.x > 0.0f && input.box.size.y > 0.0f) {
            float low = std::max(1.0f, input.style.min_size);
            float high = std::max(low, input.style.size);
            if (input.style.max_size > 0.0f) {
                high = std::min(high, input.style.max_size);
            }
            float best_size = low;

            for (int step = 0; step < 8; ++step) {
                const float mid = (low + high) * 0.5f;
                TextLayoutResult temp = measure_with_size(mid);
                if (temp.size.x <= input.box.size.x && temp.size.y <= input.box.size.y) {
                    best_size = mid;
                    low = mid + 0.1f;
                } else {
                    high = mid - 0.1f;
                }
                if (high < low) break;
            }

            current_input.style.size = best_size;
        }

        return current_input.runs.empty()
            ? layout_single_run(current_input)
            : layout_inline_runs(current_input);
    }
};

} // namespace chronon3d
